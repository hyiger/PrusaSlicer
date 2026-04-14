///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "BedMeshSerial.hpp"
#include "Serial.hpp"

#include <boost/asio.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <thread>

namespace Slic3r {
namespace Utils {

namespace asio = boost::asio;
using boost::system::error_code;
using Slic3r::GUI::BedMeshData;

// Read from the serial port until we see a line equal to "ok" or the deadline
// expires. Returns the accumulated response split into lines (not including
// the terminal "ok").
static bool read_until_ok(Serial& serial,
                          asio::io_context& io,
                          std::vector<std::string>& out_lines,
                          unsigned timeout_ms,
                          std::string& error)
{
    asio::deadline_timer timer(io);
    std::string buffer;
    bool        saw_ok      = false;
    bool        timed_out   = false;
    char        ch          = 0;

    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);

    while (!saw_ok && !timed_out) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) {
            timed_out = true;
            break;
        }

        io.restart();
        bool read_done = false;
        error_code  read_ec;

        asio::async_read(serial, asio::buffer(&ch, 1),
            [&](const error_code& ec, size_t) {
                read_ec   = ec;
                read_done = true;
                timer.cancel();
            });

        timer.expires_from_now(boost::posix_time::milliseconds(remaining));
        timer.async_wait([&](const error_code& ec) {
            if (!ec) { // fired, not cancelled
                timed_out = true;
                serial.cancel();
            }
        });

        io.run();

        if (!read_done || read_ec || timed_out)
            break;

        if (ch == '\r')
            continue;
        if (ch == '\n') {
            if (buffer == "ok")
                saw_ok = true;
            else if (!buffer.empty())
                out_lines.push_back(std::move(buffer));
            buffer.clear();
        } else {
            buffer.push_back(ch);
        }
    }

    if (!saw_ok) {
        if (!buffer.empty())
            out_lines.push_back(std::move(buffer));
        error = timed_out ? "Timed out waiting for 'ok' from printer"
                          : "Serial read failed before seeing 'ok'";
        return false;
    }
    return true;
}

// Send a command terminated by '\n' and read response until "ok".
static bool send_and_wait(Serial& serial,
                          asio::io_context& io,
                          const std::string& cmd,
                          std::vector<std::string>& out_lines,
                          unsigned timeout_ms,
                          std::string& error)
{
    const std::string full = cmd + "\n";
    error_code ec;
    asio::write(serial, asio::buffer(full), ec);
    if (ec) {
        error = "Serial write failed: " + ec.message();
        return false;
    }
    return read_until_ok(serial, io, out_lines, timeout_ms, error);
}

// Query M115 and return the expected G29 probe count for known printers,
// or 0 if unknown (caller falls back to pulse-mode progress).
//
// Values come directly from Buddy firmware's per-printer config files
// (GRID_MAJOR_POINTS_X × GRID_MAJOR_POINTS_Y). The firmware probes this
// "major" grid and interpolates to the finer GRID_MAX_POINTS output.
// See Prusa-Firmware-Buddy/include/marlin/Configuration_*.h.
static int detect_expected_probe_count(Serial& serial, asio::io_context& io)
{
    std::string err;
    std::vector<std::string> lines;
    if (!send_and_wait(serial, io, "M115", lines, 3'000, err))
        return 0;
    for (const auto& line : lines) {
        fprintf(stderr, "[BedMesh probe] M115 << %s\n", line.c_str());
        // Look for MACHINE_TYPE:... in Buddy firmware M115 output.
        auto pos = line.find("MACHINE_TYPE:");
        if (pos == std::string::npos)
            continue;
        std::string tail = line.substr(pos + 13);
        auto contains_ci = [&](const char* needle) {
            std::string hay = tail;
            std::transform(hay.begin(), hay.end(), hay.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            std::string n = needle;
            std::transform(n.begin(), n.end(), n.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            return hay.find(n) != std::string::npos;
        };
        // XL:  12×12 grid = 144 (Configuration_XL.h)
        if (contains_ci("XL"))
            return 144;
        // iX:   9×9 grid  =  81 (Configuration_iX.h)
        if (contains_ci("iX"))
            return 81;
        // MINI: 4×4 grid  =  16 (Configuration_MINI.h)
        if (contains_ci("MINI"))
            return 16;
        // Core One / MK4 / MK4S / MK3.5: 7×7 grid = 49
        if (contains_ci("Core-One") || contains_ci("CoreOne") ||
            contains_ci("MK4")      || contains_ci("MK3.5"))
            return 49;
        break;
    }
    return 0; // unknown model → pulse-mode progress
}

// Long-running read: streams lines, calls line_cb per non-empty line, honors
// both an overall deadline and an idle timeout (deadline since last byte),
// and polls cancel_requested between reads. Returns true iff an "ok" line
// was received.
static bool stream_until_ok(Serial& serial,
                            asio::io_context& io,
                            unsigned overall_timeout_ms,
                            unsigned idle_timeout_ms,
                            std::atomic<bool>* cancel_requested,
                            const std::function<void(const std::string&)>& line_cb,
                            std::string& error)
{
    asio::deadline_timer timer(io);
    std::string buffer;
    bool saw_ok = false;
    char ch = 0;

    const auto start  = std::chrono::steady_clock::now();
    auto last_byte_at = start;

    while (!saw_ok) {
        if (cancel_requested && cancel_requested->load()) {
            error = "Cancelled";
            return false;
        }
        const auto now = std::chrono::steady_clock::now();
        const long overall_left = overall_timeout_ms -
            std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        const long idle_left = idle_timeout_ms -
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_byte_at).count();
        if (overall_left <= 0) { error = "Overall timeout elapsed"; return false; }
        if (idle_left    <= 0) { error = "Idle timeout (no firmware output)"; return false; }

        // Use the tighter of the two remaining budgets for this read slice, but
        // cap at 500ms so we can check cancel_requested periodically.
        const long slice_ms = std::min({ overall_left, idle_left, 500L });

        io.restart();
        bool read_done = false;
        bool timed_out = false;
        error_code  read_ec;

        asio::async_read(serial, asio::buffer(&ch, 1),
            [&](const error_code& ec, size_t) {
                read_ec = ec;
                read_done = true;
                timer.cancel();
            });
        timer.expires_from_now(boost::posix_time::milliseconds(slice_ms));
        timer.async_wait([&](const error_code& ec) {
            if (!ec) { timed_out = true; serial.cancel(); }
        });
        io.run();

        // Slice timer fired: the async_read was cancelled, so the read handler
        // will have run with operation_aborted and read_done=true / read_ec set.
        // That's expected — loop and reevaluate the overall/idle budgets.
        if (timed_out)
            continue;

        if (!read_done || read_ec) {
            error = std::string("Serial read error")
                  + (read_ec ? std::string(": ") + read_ec.message() : std::string());
            return false;
        }

        last_byte_at = std::chrono::steady_clock::now();

        if (ch == '\r')
            continue;
        if (ch == '\n') {
            if (buffer == "ok") {
                saw_ok = true;
                break;
            }
            if (!buffer.empty() && line_cb)
                line_cb(buffer);
            buffer.clear();
        } else {
            buffer.push_back(ch);
        }
    }
    return saw_ok;
}

static std::string pick_prusa_port()
{
    auto ports = scan_serial_ports_extended();
    // Prefer a device explicitly tagged as an Original Prusa printer.
    for (const auto& p : ports)
        if (p.is_printer)
            return p.port;
    // Fallback: match on Prusa Research USB VID 0x2C99 (11417 decimal).
    for (const auto& p : ports)
        if (p.id_vendor == 0x2C99)
            return p.port;
    return {};
}

BedMeshFetchResult fetch_bed_mesh_from_printer(const Vec2d& probe_min,
                                               const Vec2d& probe_max,
                                               const std::string& explicit_port,
                                               unsigned timeout_ms)
{
    BedMeshFetchResult result;

    std::string port = explicit_port.empty() ? pick_prusa_port() : explicit_port;
    if (port.empty()) {
        result.error = "No Prusa printer found on USB serial. "
                       "Make sure it is connected and not busy with another app.";
        return result;
    }
    result.port_used = port;

    try {
        asio::io_context io;
        Serial serial(io, port, 115200);

        // Brief settle: some USB-CDC devices drop the first few bytes.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Drain any unsolicited output already buffered (temperature reports, etc.)
        {
            error_code ec;
            char drain_buf[256];
            // Non-blocking drain: try to read available bytes with a tight timer.
            asio::deadline_timer t(io);
            bool drained = false;
            std::size_t n = 0;
            io.restart();
            asio::async_read(serial, asio::buffer(drain_buf, sizeof(drain_buf)),
                asio::transfer_at_least(1),
                [&](const error_code&, std::size_t got) { n = got; drained = true; t.cancel(); });
            t.expires_from_now(boost::posix_time::milliseconds(100));
            t.async_wait([&](const error_code& ec) {
                if (!ec) { serial.cancel(); drained = true; }
            });
            io.run();
            (void)drained; (void)n;
        }

        std::vector<std::string> dummy;
        std::string err;

        // Enable stored bed leveling so the mesh is actually active before we dump.
        if (!send_and_wait(serial, io, "M420 S1", dummy, timeout_ms, err)) {
            result.error = "M420 S1 failed: " + err;
            return result;
        }
        dummy.clear();

        // Dump the mesh in CSV format.
        std::vector<std::string> response;
        if (!send_and_wait(serial, io, "M420 V1 T1", response, timeout_ms, err)) {
            result.error = "M420 V1 T1 failed: " + err;
            return result;
        }

        BedMeshData mesh = BedMeshData::parse_m420_output(response, probe_min, probe_max);
        if (mesh.status != BedMeshData::Status::Loaded) {
            result.error = mesh.error_message.empty()
                ? std::string("Failed to parse mesh from printer response")
                : mesh.error_message;
            return result;
        }

        result.mesh = std::move(mesh);
        return result;
    } catch (const std::exception& e) {
        result.error = std::string("Serial error on ") + port + ": " + e.what();
        return result;
    }
}

BedMeshFetchResult probe_bed_mesh_from_printer(const Vec2d& probe_min,
                                               const Vec2d& probe_max,
                                               const ProbeProgressCallback& progress,
                                               std::atomic<bool>* cancel_requested,
                                               const std::string& explicit_port,
                                               int nozzle_temp_c)
{
    BedMeshFetchResult result;

    std::string port = explicit_port.empty() ? pick_prusa_port() : explicit_port;
    if (port.empty()) {
        result.error = "No Prusa printer found on USB serial.";
        return result;
    }
    result.port_used = port;

    // Strip firmware chatter prefixes ("echo:", "//", leading whitespace).
    auto clean = [](std::string s) {
        auto starts_with = [&](const char* p) {
            return s.compare(0, std::strlen(p), p) == 0;
        };
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
            s.erase(s.begin());
        if (starts_with("echo:")) s.erase(0, 5);
        else if (starts_with("// ")) s.erase(0, 3);
        else if (starts_with("//"))  s.erase(0, 2);
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
            s.erase(s.begin());
        return s;
    };

    auto emit = [&](const char* stage, const std::string& detail = {}, int step = 0, int total = 0) {
        if (progress) {
            BedMeshProbeProgress p;
            p.stage = stage;
            p.detail = clean(detail);
            p.step = step;
            p.total_steps = total;
            progress(p);
        }
    };

    try {
        asio::io_context io;
        Serial serial(io, port, 115200);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Drain any unsolicited output that accumulated on the CDC buffer during
        // the USB-serial reset (temperature reports, "T:..." lines, remnants of
        // a previous aborted command). Without this, the first `stream_until_ok`
        // can see stale bytes that corrupt the read state.
        {
            error_code ec;
            char       buf[1024];
            asio::deadline_timer t(io);
            bool       done = false;
            io.restart();
            asio::async_read(serial, asio::buffer(buf, sizeof(buf)),
                asio::transfer_at_least(1),
                [&](const error_code&, std::size_t n) {
                    if (n > 0)
                        fprintf(stderr, "[BedMesh probe] drained %zu bytes\n", n);
                    done = true;
                    t.cancel();
                });
            t.expires_from_now(boost::posix_time::milliseconds(250));
            t.async_wait([&](const error_code& tec) {
                if (!tec) { serial.cancel(); done = true; }
            });
            io.run();
            (void)done;
        }

        std::string err;

        // Step 0: query printer model so we can size the progress bar. Non-fatal
        // if this fails — we just fall back to pulse-mode progress.
        const int expected_probes = detect_expected_probe_count(serial, io);
        fprintf(stderr, "[BedMesh probe] expected_probes=%d\n", expected_probes);

        // Step 1: home all axes.
        emit("Homing", "G28");
        {
            error_code ec;
            asio::write(serial, asio::buffer(std::string("G28\n")), ec);
            if (ec) { result.error = "Write G28 failed: " + ec.message(); return result; }
            fprintf(stderr, "[BedMesh probe] >> G28\n");
            if (!stream_until_ok(serial, io, /*overall*/ 180'000, /*idle*/ 60'000,
                    cancel_requested,
                    [&](const std::string& line) {
                        fprintf(stderr, "[BedMesh probe] G28 << %s\n", line.c_str());
                        if (line.find("busy") == std::string::npos)
                            emit("Homing", line);
                    }, err)) {
                result.error = "G28 failed: " + err;
                return result;
            }
            fprintf(stderr, "[BedMesh probe] G28 done (ok)\n");
        }

        // Step 2: start heating nozzle (non-blocking), then wait for temperature.
        emit("Heating", std::to_string(nozzle_temp_c) + " °C target");
        {
            error_code ec;
            const std::string cmd = "M104 S" + std::to_string(nozzle_temp_c) + "\n";
            asio::write(serial, asio::buffer(cmd), ec);
            if (ec) { result.error = "Write M104 failed: " + ec.message(); return result; }
            fprintf(stderr, "[BedMesh probe] >> %s", cmd.c_str());
            if (!stream_until_ok(serial, io, /*overall*/ 15'000, /*idle*/ 10'000,
                    cancel_requested,
                    [&](const std::string& line) {
                        fprintf(stderr, "[BedMesh probe] M104 << %s\n", line.c_str());
                    }, err)) {
                result.error = "M104 failed: " + err;
                return result;
            }
            fprintf(stderr, "[BedMesh probe] M104 done (ok)\n");
        }
        {
            error_code ec;
            const std::string cmd = "M109 S" + std::to_string(nozzle_temp_c) + "\n";
            asio::write(serial, asio::buffer(cmd), ec);
            if (ec) { result.error = "Write M109 failed: " + ec.message(); return result; }
            fprintf(stderr, "[BedMesh probe] >> %s", cmd.c_str());
            if (!stream_until_ok(serial, io, /*overall*/ 300'000, /*idle*/ 60'000,
                    cancel_requested,
                    [&](const std::string& line) {
                        fprintf(stderr, "[BedMesh probe] M109 << %s\n", line.c_str());
                        // Firmware temperature status lines look like:
                        //   " T:169.55/170.00 B:25.03/0.00 X:... A:... @:86 ..."
                        // Extract just the nozzle current/target.
                        const auto t = line.find("T:");
                        if (t != std::string::npos) {
                            float cur = 0.f, tgt = 0.f;
                            if (std::sscanf(line.c_str() + t, "T:%f/%f", &cur, &tgt) == 2) {
                                char buf[48];
                                std::snprintf(buf, sizeof(buf), "%.0f / %.0f \xc2\xb0""C", cur, tgt);
                                emit("Heating", buf);
                            }
                        }
                    }, err)) {
                result.error = "M109 failed: " + err;
                return result;
            }
            fprintf(stderr, "[BedMesh probe] M109 done (ok)\n");
        }

        // Step 3: probe. The 7×7 printers (Core One / MK4 / MINI) complete ~49
        // points; the XL probes each heatbed tile and lands much higher. When
        // expected_probes is 0 (unknown / XL) the progress dialog pulses.
        emit("Probing", "G29 (this takes ~2 minutes)", 0, expected_probes);
        {
            error_code ec;
            asio::write(serial, asio::buffer(std::string("G29\n")), ec);
            if (ec) { result.error = "Write G29 failed: " + ec.message(); return result; }
            int probes_ok = 0;
            if (!stream_until_ok(serial, io, /*overall*/ 600'000, /*idle*/ 60'000,
                    cancel_requested,
                    [&](const std::string& line) {
                        fprintf(stderr, "[BedMesh probe] G29 << %s\n", line.c_str());
                        // Count successful probes for progress. If actual count
                        // exceeds what we expected, drop to pulse (total=0) so
                        // we don't overflow past 100%.
                        auto effective_total = [&]() {
                            return (expected_probes > 0 && probes_ok <= expected_probes)
                                 ? expected_probes : 0;
                        };
                        if (line.find("Probe classified as clean and OK") != std::string::npos) {
                            ++probes_ok;
                            char buf[48];
                            const int total = effective_total();
                            if (total > 0)
                                std::snprintf(buf, sizeof(buf), "Point %d of %d", probes_ok, total);
                            else
                                std::snprintf(buf, sizeof(buf), "Point %d", probes_ok);
                            emit("Probing", buf, probes_ok, total);
                        } else if (line.find("Extrapolating") != std::string::npos) {
                            emit("Probing", "Extrapolating mesh…", probes_ok, effective_total());
                        } else if (line.find("Insufficient") != std::string::npos) {
                            emit("Probing", "Insufficient probe data", probes_ok, effective_total());
                        }
                        // Skip the noisy "busy: processing", "Re-tared", "Starting probe"
                        // lines — they show up on every point and clutter the dialog.
                    }, err)) {
                result.error = "G29 failed: " + err;
                return result;
            }
        }

        // Step 4: cool nozzle (fire-and-forget, don't block on it).
        {
            error_code ec;
            asio::write(serial, asio::buffer(std::string("M104 S0\n")), ec);
            std::vector<std::string> dummy;
            stream_until_ok(serial, io, 5'000, 5'000, cancel_requested, nullptr, err);
        }

        // Step 5: enable & dump mesh.
        emit("Reading mesh", "M420 V1 T1");
        std::vector<std::string> dummy, response;
        if (!send_and_wait(serial, io, "M420 S1", dummy, 5'000, err)) {
            result.error = "M420 S1 failed: " + err;
            return result;
        }
        if (!send_and_wait(serial, io, "M420 V1 T1", response, 10'000, err)) {
            result.error = "M420 V1 T1 failed: " + err;
            return result;
        }

        Slic3r::GUI::BedMeshData mesh =
            Slic3r::GUI::BedMeshData::parse_m420_output(response, probe_min, probe_max);
        if (mesh.status != Slic3r::GUI::BedMeshData::Status::Loaded) {
            result.error = mesh.error_message.empty()
                ? std::string("Failed to parse probed mesh")
                : mesh.error_message;
            return result;
        }
        result.mesh = std::move(mesh);
        return result;
    } catch (const std::exception& e) {
        result.error = std::string("Serial error on ") + port + ": " + e.what();
        return result;
    }
}

} // namespace Utils
} // namespace Slic3r
