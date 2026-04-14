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

        if (timed_out && !read_done)
            continue; // loop, reevaluate budgets

        if (!read_done || read_ec) {
            error = "Serial read error";
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

    auto emit = [&](const char* stage, const std::string& detail = {}, int step = 0, int total = 0) {
        if (progress) {
            BedMeshProbeProgress p;
            p.stage = stage;
            p.detail = detail;
            p.step = step;
            p.total_steps = total;
            progress(p);
        }
    };

    try {
        asio::io_context io;
        Serial serial(io, port, 115200);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        std::string err;

        // Step 1: home all axes.
        emit("Homing", "G28");
        {
            error_code ec;
            asio::write(serial, asio::buffer(std::string("G28\n")), ec);
            if (ec) { result.error = "Write G28 failed: " + ec.message(); return result; }
            if (!stream_until_ok(serial, io, /*overall*/ 180'000, /*idle*/ 60'000,
                    cancel_requested,
                    [&](const std::string& line) {
                        if (line.find("busy") == std::string::npos)
                            emit("Homing", line);
                    }, err)) {
                result.error = "G28 failed: " + err;
                return result;
            }
        }

        // Step 2: start heating nozzle (non-blocking), then wait for temperature.
        emit("Heating", std::to_string(nozzle_temp_c) + " °C target");
        {
            error_code ec;
            const std::string cmd = "M104 S" + std::to_string(nozzle_temp_c) + "\n";
            asio::write(serial, asio::buffer(cmd), ec);
            if (ec) { result.error = "Write M104 failed: " + ec.message(); return result; }
            if (!stream_until_ok(serial, io, 5'000, 5'000, cancel_requested,
                                 nullptr, err)) {
                result.error = "M104 failed: " + err;
                return result;
            }
        }
        {
            error_code ec;
            const std::string cmd = "M109 S" + std::to_string(nozzle_temp_c) + "\n";
            asio::write(serial, asio::buffer(cmd), ec);
            if (ec) { result.error = "Write M109 failed: " + ec.message(); return result; }
            if (!stream_until_ok(serial, io, /*overall*/ 300'000, /*idle*/ 30'000,
                    cancel_requested,
                    [&](const std::string& line) {
                        // Temperature echo lines are informative: " T:123.4/170.0 ..."
                        if (!line.empty() && line.front() == ' ')
                            emit("Heating", line);
                    }, err)) {
                result.error = "M109 failed: " + err;
                return result;
            }
        }

        // Step 3: probe. Core One's UBL uses a 7×7 coarse probe (49 points) that
        // gets interpolated to the 21×21 output grid.
        emit("Probing", "G29 (this takes ~2 minutes)", 0, 49);
        {
            error_code ec;
            asio::write(serial, asio::buffer(std::string("G29\n")), ec);
            if (ec) { result.error = "Write G29 failed: " + ec.message(); return result; }
            int probes_ok = 0;
            if (!stream_until_ok(serial, io, /*overall*/ 600'000, /*idle*/ 60'000,
                    cancel_requested,
                    [&](const std::string& line) {
                        // Count successful probes for progress
                        if (line.find("Probe classified as clean and OK") != std::string::npos) {
                            ++probes_ok;
                            emit("Probing", line, probes_ok, 49);
                        } else if (line.find("Starting probe") != std::string::npos
                                || line.find("Extrapolating") != std::string::npos
                                || line.find("Insufficient") != std::string::npos
                                || line.find("Re-tared") != std::string::npos) {
                            emit("Probing", line, probes_ok, 49);
                        }
                        // Skip the noisy "busy: processing" lines
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
