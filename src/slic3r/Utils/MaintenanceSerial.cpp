///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "MaintenanceSerial.hpp"
#include "Serial.hpp"

#include <boost/asio.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

// The cold-pull recipe below was verified line-by-line against
// Prusa-Firmware-Buddy v6.6.3 (COREONE_INDX):
//  - Heating requires a picked/latched tool (base_hotend.cpp:50) → T<n> first.
//  - G1 E below EXTRUDE_MINTEMP 170 is silently stripped unless M302 S0
//    (planner.cpp:2280).
//  - M0 = QuickPause dialog on the printer LCD with a custom message; it holds
//    only the gcode queue, tool stays picked+heated (marlin_stubs/M0.cpp).
//    Messages: whole line ≤95 chars (MAX_CMD_SIZE 96), first token a plain
//    word, no semicolons (parser string_arg fallback).
//  - M109 R regulates AT the target and can exit early when the cooling slope
//    flattens (<1.5 °C/min) → follow with M104 S0 + fixed dwell.
//  - The pull retract cannot unlock the nozzle away from the dock; the lock is
//    mechanical and dock-gated (toolchanger_indx.cpp:525-575).
//  - Firmware E current default is 550 mA; 650 is what the firmware itself
//    uses for lock moves, safe for the stiff-plug pull (M906).

namespace Slic3r {
namespace Utils {

namespace asio = boost::asio;
using boost::system::error_code;

// ---------------------------------------------------------------------------
// Transport helpers. These mirror the file-static helpers in BedMeshSerial.cpp
// (deliberately self-contained, same as that feature, so neither depends on
// the other's internals).
// ---------------------------------------------------------------------------

static std::string explain_serial_error(const std::string& port, const std::string& what)
{
    std::string lower = what;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (lower.find("permission denied") != std::string::npos ||
        lower.find("access is denied")  != std::string::npos)
        return "Port " + port + " is in use by another application "
               "(OctoPrint, Pronterface, PrusaConnect, or another slicer).";
    if (lower.find("resource busy")     != std::string::npos ||
        lower.find("device or resource busy") != std::string::npos)
        return "Port " + port + " is busy. Close any other application that "
               "might be connected to the printer and try again.";
    if (lower.find("no such file")      != std::string::npos ||
        lower.find("no such device")    != std::string::npos)
        return "Port " + port + " disappeared. Is the printer still plugged in?";
    return "Serial error on " + port + ": " + what;
}

static std::string pick_prusa_port()
{
    auto ports = scan_serial_ports_extended();
    for (const auto& p : ports)
        if (p.is_printer)
            return p.port;
    for (const auto& p : ports)
        if (p.id_vendor == 0x2C99) // Prusa Research USB VID
            return p.port;
    return {};
}

// Long-running read: streams lines, calls line_cb per non-empty line, honors
// an overall deadline, an idle timeout (since last byte), and a cancel flag.
// Returns true iff an "ok" line was received. Identical in behaviour to
// BedMeshSerial's stream_until_ok.
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

// Drain unsolicited output buffered on the CDC endpoint (temperature reports,
// remnants of an aborted command) so the first stream doesn't see stale bytes.
static void drain_serial(Serial& serial, asio::io_context& io)
{
    char buf[1024];
    asio::deadline_timer t(io);
    bool done = false;
    io.restart();
    asio::async_read(serial, asio::buffer(buf, sizeof(buf)),
        asio::transfer_at_least(1),
        [&](const error_code&, std::size_t n) {
            if (n > 0)
                BOOST_LOG_TRIVIAL(debug) << "[Maintenance] drained " << n << " bytes";
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

// ---------------------------------------------------------------------------
// Cold pull as a standalone G-code file (print-job delivery)
// ---------------------------------------------------------------------------

std::string generate_cold_pull_gcode(const ColdPullOptions& options)
{
    const std::string tool  = std::to_string(options.tool);
    const std::string flush = std::to_string(options.flush_temp_c);
    const std::string pull  = std::to_string(options.pull_temp_c);
    // Nozzle number as printed on the machine, for human-facing text only.
    const std::string nozzle = std::to_string(options.tool + 1);

    std::ostringstream g;

    g << "; ============================================================\n"
      << "; INDX cold pull - guided, with on-screen prompts\n"
      << "; Generated by PrusaSlicer. Nozzle " << nozzle
      << ", flush " << flush << "C, pull " << pull << "C\n"
      << "; ------------------------------------------------------------\n"
      << "; BEFORE RUNNING, at the printer:\n"
      << ";   - Settings: turn the Filament Sensor OFF\n"
      << ";   - Settings: turn Auto Retract OFF\n"
      << ";   - Remove the PTFE tube from this tool\n"
      << ";   - Have light-colored PLA ready and stay at the printer\n"
      << "; ------------------------------------------------------------\n"
      << "; The procedure stops at several prompts and waits for a knob\n"
      << "; press. To bail out, press the knob then Stop the print.\n"
      << "; If you Stop at the tip check, the restore block never runs -\n"
      << "; afterwards send M302 S170 and M591 R, or power-cycle, to put\n"
      << "; back the cold-extrusion guard and stuck-filament detection.\n"
      << "; A prompt left ~30 min turns the heaters off (safety timer).\n"
      << "; ============================================================\n\n";

    g << "M117 Cold pull - guided\n"
      << "M0 Setup check: filament sensor OFF, auto retract OFF, PTFE tube removed. Press to continue\n\n";

    g << "M117 Picking nozzle " << nozzle << "\n"
      << "T" << tool << " M0                 ; M0 param = ignore tool mapping\n"
      << "M302 S0               ; allow cold extrusion for the session\n"
      << "M83                   ; relative E\n"
      << "M591 S0               ; disable stuck-filament detection\n\n";

    g << "M0 Insert light PLA into the top port until it stops at the drive gears. Do not force it\n\n";

    g << "M117 Heating to " << flush << "\n"
      << "M109 S" << flush << "\n\n";

    g << "M117 Purging - watch the tip\n";
    for (int i = 0; i < 3; ++i) {
        g << "G1 E20 F120\n";
        if (i < 2) g << "G4 S2\n";
    }
    g << "\n";

    g << "M0 Tip check. PLA out: press knob to continue. NOTHING out: press knob then Stop print\n"
      << "G4 S15                ; grace window to reach Stop before packing starts\n"
      << "; If nothing extruded, the blockage is above the melt zone and a\n"
      << "; cold pull cannot reach it.\n\n";

    g << "M117 Cooling - packing\n"
      << "M104 S180             ; fall toward 180, packing pushes on the way\n";
    for (int i = 0; i < 8; ++i) {
        g << "G1 E2 F60\n";
        if (i < 7) g << "G4 S5\n";
    }
    g << "\n";

    g << "M117 Deep cooling to 60\n"
      << "M104 S0\n"
      << "M106 S255             ; fan full\n"
      << "M109 R60              ; regulates AT 60; may exit early on slope-break\n"
      << "M104 S0               ; heater truly off\n"
      << "G4 S120               ; dwell covers an early exit above 60\n"
      << "M107\n\n";

    g << "M117 Reheating to " << pull << "\n"
      << "M104 S" << pull << "\n"
      << "M109 R" << pull << "\n\n";

    g << "M0 Pull ready at " << pull
      << "C. Motor will yank the plug - keep hands clear. Press to pull\n\n";

    g << "M117 PULLING\n"
      << "M906 E650             ; E current up for the stiff plug\n"
      << "G1 E-40 F3000         ; 50mm/s yank\n"
      << "G1 E-30 F1200         ; feed the strand up out of the gear\n"
      << "M906 E550             ; restore INDX default current\n"
      << "M591 R                ; restore stuck-filament detection\n"
      << "M302 S170             ; restore cold-extrusion guard\n"
      << "M104 S0\n\n";

    g << "M0 Pull done. Remove strand from top port. Pressing knob starts auto wipe and dock\n\n";

    g << "M117 Finishing - hands off\n"
      << "; The end-of-print sequence runs automatically after the last line:\n"
      << "; nozzle wipe, tool dock, steppers off. Wait for Finished.\n"
      << "; Inspect the tip: three thin strands + debris = a good pull.\n"
      << "; Repeat until the tip comes out clean, then re-enable the\n"
      << "; Filament Sensor and Auto Retract and refit the PTFE tube.\n";

    return g.str();
}

// ---------------------------------------------------------------------------
// Cold pull over serial
// ---------------------------------------------------------------------------

MaintenanceResult run_cold_pull(const MaintenanceProgressCallback& progress,
                                const ColdPullOptions& options)
{
    MaintenanceResult result;

    std::string port = options.explicit_port.empty() ? pick_prusa_port()
                                                     : options.explicit_port;
    if (port.empty()) {
        result.error = "No Prusa printer found on USB serial. "
                       "Make sure it is connected and not busy with another app.";
        return result;
    }
    result.port_used = port;

    // Combined cancel flag fed by a watchdog, same pattern as the bed probe:
    // stream_until_ok keeps its single-atomic API while we honor both flags.
    std::atomic<bool> combined_cancel{ false };
    std::atomic<bool> watchdog_stop { false };
    std::thread watchdog([&]() {
        while (!watchdog_stop.load()) {
            if (options.cancel_requested && options.cancel_requested->load())
                combined_cancel.store(true);
            if (options.force_stop_requested && options.force_stop_requested->load())
                combined_cancel.store(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    struct WatchdogJoiner {
        std::atomic<bool>& stop; std::thread& t;
        ~WatchdogJoiner() { stop.store(true); if (t.joinable()) t.join(); }
    } joiner{ watchdog_stop, watchdog };

    auto is_force_stop = [&]() {
        return options.force_stop_requested && options.force_stop_requested->load();
    };
    auto is_cancel = [&]() {
        return combined_cancel.load();
    };

    // Strip firmware chatter prefixes for display.
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

    // The procedure has a fixed step list; progress is slicer-driven (we know
    // which command we're on), with firmware temperature lines as detail.
    int step = 0;
    constexpr int kTotalSteps = 12;
    auto emit = [&](const char* stage, const std::string& detail = {}) {
        if (progress) {
            MaintenanceProgress p;
            p.stage = stage;
            p.detail = clean(detail);
            p.step = step;
            p.total_steps = kTotalSteps;
            progress(p);
        }
    };

    try {
        asio::io_context io;
        Serial serial(io, port, 115200);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        drain_serial(serial, io);

        std::string err;

        auto send_emergency_stop = [&]() {
            error_code ec;
            asio::write(serial, asio::buffer(std::string("M112\n")), ec);
            BOOST_LOG_TRIVIAL(warning) << "[Maintenance] >> M112 (force stop)";
        };

        // Send one command and stream to "ok". `stage` drives the progress
        // dialog and `detail` describes the step in plain language. The caption
        // is emitted ONCE and then left alone: firmware chatter (temperature
        // reports and the like) goes to the debug log only, so the dialog shows
        // one stable line instead of a churning wall of text.
        auto run = [&](const char* stage, const std::string& cmd,
                       const char* detail,
                       unsigned overall_ms, unsigned idle_ms) -> bool {
            emit(stage, detail);
            error_code ec;
            asio::write(serial, asio::buffer(cmd + "\n"), ec);
            if (ec) {
                result.error = "Write failed (" + cmd + "): " + ec.message();
                return false;
            }
            BOOST_LOG_TRIVIAL(debug) << "[Maintenance] >> " << cmd;
            if (!stream_until_ok(serial, io, overall_ms, idle_ms, &combined_cancel,
                    [&](const std::string& line) {
                        BOOST_LOG_TRIVIAL(debug) << "[Maintenance] << " << line;
                    }, err)) {
                if (is_force_stop()) {
                    send_emergency_stop();
                    result.error = "Force-stopped by user (printer requires reset).";
                    return false;
                }
                if (is_cancel()) {
                    result.cancelled = true;
                    result.error = "Cancelled";
                    return false;
                }
                result.error = std::string(stage) + " failed (" + cmd + "): " + err;
                return false;
            }
            return true;
        };

        // Best-effort state restore for the cooperative-cancel path. Short
        // timeouts, errors ignored: if an M0 dialog is still up on the printer
        // these queue behind it and execute once the user presses the knob.
        auto restore_printer_state = [&]() {
            emit("Cancelling", "Restoring printer state");
            const char* cleanup[] = {
                "M104 S0",   // heater off
                "M107",      // fan off
                "M302 S170", // restore cold-extrusion guard
                "M591 R",    // restore stuck-filament detection from EEPROM
                "M906 E550", // restore INDX default E current
                "M84",       // release steppers
            };
            for (const char* cmd : cleanup) {
                error_code ec;
                asio::write(serial, asio::buffer(std::string(cmd) + "\n"), ec);
                if (ec) return; // port gone; nothing more we can do
                std::string ignore_err;
                std::vector<std::string> sink;
                (void)stream_until_ok(serial, io, 5'000, 5'000, nullptr,
                                      [](const std::string&) {}, ignore_err);
            }
        };

        // Timeout tiers. M0 prompt waits are user-paced: Buddy emits
        // "echo:busy:" keepalives while the dialog is up, so the idle timer
        // keeps refreshing; the overall cap just bounds a walked-away session
        // (the printer's own 30-min safety timer will have killed the heaters
        // long before this expires).
        constexpr unsigned kQuick   =    10'000; // parameter/setup commands
        constexpr unsigned kMove    =    60'000; // short E moves, dwells
        constexpr unsigned kPick    =   240'000; // T<n>: may home + travel
        constexpr unsigned kHeat    =   900'000; // M109 heat-up
        constexpr unsigned kCool    = 2'400'000; // M109 R deep-cool (fan-assisted)
        constexpr unsigned kPrompt  = 7'200'000; // M0 user prompts (2 h)
        constexpr unsigned kIdle    =    30'000; // per-byte idle cap everywhere

        const std::string tool  = std::to_string(options.tool);
        const std::string flush = std::to_string(options.flush_temp_c);
        const std::string pull  = std::to_string(options.pull_temp_c);

        // --- SERIAL-PRINT TIMEOUT DISARM. Read this before touching the
        // command list below; it caused a BSOD ("E move without tool") on real
        // hardware.
        //
        // Buddy treats any G-command (and M73/M74/M109/M190) arriving over
        // serial as the start of an implicit "serial print"
        // (serial_printing.cpp:56-99). The inactivity timestamp is stamped when
        // that command is QUEUED, but the only thing that refreshes it,
        // SerialPrinting::print_loop(), runs solely from State::Printing --
        // which cannot be reached until the arming command finishes
        // (marlin_server.cpp:2207-2210).
        //
        // So arming with a LONG-BLOCKING command (M109 heat-up, or a G1 E move
        // that takes >5s) means the first timeout evaluation already sees the
        // whole blocking duration elapsed, instantly exceeding the 5s
        // serial_printing_screen_timeout (serial_printing.hpp:33). That runs
        // serial_print_finalize() -> pre_finalize_print(): auto-retract, nozzle
        // cleaner wipe, tool_change(NoTool) DOCK, disable_e_steppers. Every
        // later E move then hits bsod("E move without tool") at
        // planner.cpp:2177.
        //
        // Fix: arm with an INSTANT G-command first. G4 P1 (1 millisecond dwell)
        // is print-indicating via the 'G' branch, completes immediately, and so
        // lets the state reach Printing with a fresh timestamp. From then on the
        // timer is refreshed continuously, because GCodeQueue::advance()
        // decrements the queue length only AFTER process_next_command()
        // returns -- so has_commands_queued() stays true for the whole of a
        // blocking M109/M0/G4, and long user prompts are safe.
        //
        // Belt and braces: the pre-flight dialog also asks the user to turn off
        // Settings > Hardware > Experimental Settings > "Serial Printing
        // Screen", which stops serial_command_hook() from ever arming
        // (serial_printing.cpp:91-93); and re_pick below re-asserts the tool
        // before each extrusion block so a dock can never be followed by a
        // naked E move.
        const std::string arm_serial_print = "G4 P1";

        // Re-assert the tool immediately before any block of E moves.
        // tool_change repopulates tools.physical_tool, so even if something
        // docked the tool behind our back the next E move cannot hit the
        // assert. Idempotent when the tool is already picked.
        const std::string re_pick = "T" + tool + " M0";

        // --- The guided procedure. M0 messages: ≤95 chars, plain first word,
        // --- no semicolons (Buddy parser + MAX_CMD_SIZE rules).
        // `detail` is what the user reads in the progress dialog -- describe
        // the intent, never echo the gcode. The raw command still goes to the
        // debug log for diagnosis.
        struct Step {
            const char* stage;
            std::string cmd;
            const char* detail;
            unsigned    overall;
        };
        const Step steps[] = {
            { "Waiting at printer",
              "M0 Setup check: filament sensor OFF, auto retract OFF, PTFE tube removed. Press to continue",
              "Confirm the setup prompt on the printer screen", kPrompt },
            // Arm the serial-print state machine with an instant G-command so
            // it can never be armed later by a long-blocking one. See the
            // SERIAL-PRINT TIMEOUT DISARM note above.
            { "Preparing",    arm_serial_print, "Starting session",                        kQuick },
            { "Picking tool", "T" + tool + " M0",
              "Picking nozzle from its dock",                                              kPick  },
            { "Preparing",    "M302 S0",  "Allowing cold extrusion",                       kQuick },
            { "Preparing",    "M83",      "Switching to relative extrusion",               kQuick },
            { "Preparing",    "M591 S0",  "Disabling stuck-filament detection",            kQuick },
            { "Waiting at printer",
              "M0 Insert light PLA into the top port until it stops at the drive gears. Do not force it",
              "Insert filament at the printer, then press the knob",                       kPrompt },
            { "Heating",      "M109 S" + flush,
              "Heating nozzle to flush temperature",                                       kHeat  },
            { "Purging",      re_pick,       "Confirming nozzle is picked",                kPick  },
            { "Purging",      "G1 E20 F120", "Pushing filament through (1 of 3)",          kMove  },
            { "Purging",      "G4 S2",       "Letting pressure settle",                    kMove  },
            { "Purging",      "G1 E20 F120", "Pushing filament through (2 of 3)",          kMove  },
            { "Purging",      "G4 S2",       "Letting pressure settle",                    kMove  },
            { "Purging",      "G1 E20 F120", "Pushing filament through (3 of 3)",          kMove  },
            { "Waiting at printer",
              // The bail-out here is the slicer's Cancel button: over serial we
              // can restore printer state on cancel, unlike the print-job flow.
              "M0 Tip check. PLA out: press knob to continue. NOTHING out: press knob then cancel in slicer",
              "Check the nozzle tip, then answer the prompt on the printer",               kPrompt },
            { "Packing",      "M104 S180",   "Cooling toward 180 C while packing",         kQuick },
            { "Packing",      re_pick,       "Confirming nozzle is picked",                kPick  },
            { "Packing",      "G1 E2 F60",   "Packing the melt zone (1 of 8)",             kMove  },
            { "Packing",      "G4 S5",       "Cooling 5 seconds",                          kMove  },
            { "Packing",      "G1 E2 F60",   "Packing the melt zone (2 of 8)",             kMove  },
            { "Packing",      "G4 S5",       "Cooling 5 seconds",                          kMove  },
            { "Packing",      "G1 E2 F60",   "Packing the melt zone (3 of 8)",             kMove  },
            { "Packing",      "G4 S5",       "Cooling 5 seconds",                          kMove  },
            { "Packing",      "G1 E2 F60",   "Packing the melt zone (4 of 8)",             kMove  },
            { "Packing",      "G4 S5",       "Cooling 5 seconds",                          kMove  },
            { "Packing",      "G1 E2 F60",   "Packing the melt zone (5 of 8)",             kMove  },
            { "Packing",      "G4 S5",       "Cooling 5 seconds",                          kMove  },
            { "Packing",      "G1 E2 F60",   "Packing the melt zone (6 of 8)",             kMove  },
            { "Packing",      "G4 S5",       "Cooling 5 seconds",                          kMove  },
            { "Packing",      "G1 E2 F60",   "Packing the melt zone (7 of 8)",             kMove  },
            { "Packing",      "G4 S5",       "Cooling 5 seconds",                          kMove  },
            { "Packing",      "G1 E2 F60",   "Packing the melt zone (8 of 8)",             kMove  },
            { "Deep cooling", "M104 S0",     "Turning the heater off",                     kQuick },
            { "Deep cooling", "M106 S255",   "Fan to full",                                kQuick },
            { "Deep cooling", "M109 R60",    "Cooling to 60 C",                            kCool  },
            { "Deep cooling", "M104 S0",     "Turning the heater off",                     kQuick },
            { "Deep cooling", "G4 S120",     "Holding 2 minutes to finish cooling",        180'000 },
            { "Deep cooling", "M107",        "Fan off",                                    kQuick },
            { "Reheating",    "M104 S" + pull, "Setting pull temperature",                 kQuick },
            { "Reheating",    "M109 R" + pull, "Warming to pull temperature",              kHeat  },
            { "Waiting at printer",
              "M0 Pull ready at " + pull + "C. Motor will yank the plug - keep hands clear. Press to pull",
              "Keep hands clear, then press the knob to pull",                             kPrompt },
            // Re-assert the tool after the long cool/reheat waits, which are
            // the likeliest window for an unnoticed dock.
            { "Pulling",      re_pick,          "Confirming nozzle is picked",             kPick  },
            { "Pulling",      "M906 E650",      "Raising extruder motor current",          kQuick },
            { "Pulling",      "G1 E-40 F3000",  "Pulling the plug out",                    kMove  },
            { "Pulling",      "G1 E-30 F1200",  "Feeding the strand up and out",           kMove  },
            { "Restoring",    "M906 E550",      "Restoring motor current",                 kQuick },
            { "Restoring",    "M591 R",         "Restoring stuck-filament detection",      kQuick },
            { "Restoring",    "M302 S170",      "Restoring cold-extrusion guard",          kQuick },
            { "Restoring",    "M104 S0",        "Turning the heater off",                  kQuick },
            { "Waiting at printer",
              // No end-of-print machinery runs over serial: the tool stays
              // picked. Parking is a menu action on the printer.
              "M0 Done. Remove the strand and inspect the tip. Park the tool via Control menu when ready",
              "Remove the strand at the printer, then press the knob",                     kPrompt },
            { "Finishing",    "M84",            "Releasing the motors",                    kQuick },
        };

        // Map command groups onto the coarse 12-step progress scale so the
        // dialog advances meaningfully instead of once per G4.
        const char* last_stage = "";
        for (const Step& s : steps) {
            if (std::strcmp(s.stage, last_stage) != 0) {
                last_stage = s.stage;
                if (step < kTotalSteps)
                    ++step;
            }
            if (is_cancel()) {
                if (is_force_stop()) {
                    send_emergency_stop();
                    result.error = "Force-stopped by user (printer requires reset).";
                    return result;
                }
                restore_printer_state();
                result.cancelled = true;
                result.error = "Cancelled";
                return result;
            }
            if (!run(s.stage, s.cmd, s.detail, s.overall, kIdle)) {
                // On cooperative cancel mid-stream, still restore state.
                if (result.cancelled && !is_force_stop())
                    restore_printer_state();
                return result;
            }
        }

        step = kTotalSteps;
        emit("Done", "Cold pull complete");
        return result;
    } catch (const std::exception& e) {
        result.error = explain_serial_error(port, e.what());
        return result;
    }
}

} // namespace Utils
} // namespace Slic3r
