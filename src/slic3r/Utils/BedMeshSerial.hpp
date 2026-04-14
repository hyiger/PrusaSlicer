///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_BedMeshSerial_hpp_
#define slic3r_BedMeshSerial_hpp_

#include "slic3r/GUI/BedMeshData.hpp"
#include "libslic3r/Point.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace Slic3r {
namespace Utils {

struct BedMeshFetchResult
{
    Slic3r::GUI::BedMeshData mesh;
    std::string port_used;   // the port we connected to, for logging
    std::string error;       // empty on success, human-readable on failure
};

// Progress update during a multi-step probe. Called from the worker thread, so
// the UI caller is responsible for marshalling to the main thread (e.g. wxCallAfter).
struct BedMeshProbeProgress
{
    std::string stage;   // "Homing", "Heating", "Probing", "Reading mesh"
    std::string detail;  // free text: "Probe 12/49", "T:150/170 °C", firmware line, etc.
    int         step = 0;      // completed probes (Probing stage) or 0
    int         total_steps = 0; // expected probe count (Probing stage) or 0
};

using ProbeProgressCallback = std::function<void(const BedMeshProbeProgress&)>;

// Find a Prusa printer on USB serial, query the stored bed mesh via
// `M420 S1` + `M420 V1 T1`, parse the "Bed Topography Report for CSV:" block,
// and return it.
//
// probe_min/probe_max define the XY extent the mesh spans (mm). For Core One
// the firmware reports 2..248 / 3..217; other printers differ.
//
// This is synchronous and may block for up to ~timeout_ms milliseconds. Total
// typical latency: <1s when the printer has a valid stored mesh.
//
// If explicit_port is non-empty, use that port; otherwise auto-detect by
// scanning for "Original Prusa" devices.
BedMeshFetchResult fetch_bed_mesh_from_printer(const Vec2d& probe_min,
                                               const Vec2d& probe_max,
                                               const std::string& explicit_port = {},
                                               unsigned timeout_ms = 5000);

// Options controlling the probe cycle. Defaults match the historical behaviour
// (170 °C nozzle, cold bed, single tool, no force-stop).
struct BedMeshProbeOptions
{
    // Nozzle target temperature (°C). 170 is the recommended probe-safe value
    // for a PrusaProbe — hot enough to keep filament residue liquid, cold
    // enough that the bed isn't cycled between print temps.
    int nozzle_temp_c = 170;

    // Bed target temperature (°C). Heat the bed before probing so the mesh
    // reflects printing conditions. Set to 0 to skip bed heating (fast/cold
    // probe; less accurate). Typical values: 60 (PLA), 85 (PETG), 100 (ABS).
    int bed_temp_c = 0;

    // If true, probe all extruders sequentially by issuing a T<n> between
    // runs. Only meaningful on the XL. The returned BedMeshFetchResult::mesh
    // contains the LAST-probed tool's mesh; extended per-tool results are
    // emitted via progress callbacks with stage="Tool switch".
    bool probe_all_tools = false;

    // Explicit USB serial device path. Empty → auto-detect.
    std::string explicit_port;

    // Polled between reads. When set true, the function tries to stop cleanly
    // between phases (G29 itself is not interruptible — cancellation lands
    // once G29 completes).
    std::atomic<bool>* cancel_requested = nullptr;

    // Polled between reads. When set true, the function sends M112 (emergency
    // stop) and disconnects immediately. This is the nuclear option — the
    // user will need to reset the printer after. Use only when cooperative
    // cancel has failed.
    std::atomic<bool>* force_stop_requested = nullptr;
};

// Run a full bed probing cycle on the printer: G28 (home), heat bed (optional),
// heat nozzle, G29 (probe grid), cool nozzle+bed, fetch the resulting mesh via
// M420 V1 T1. Typically takes 3–5 minutes (8–10 with bed heating).
//
// progress is called from this function's thread whenever the firmware emits
// a meaningful line. Cancellation is best-effort — see BedMeshProbeOptions.
BedMeshFetchResult probe_bed_mesh_from_printer(const Vec2d& probe_min,
                                               const Vec2d& probe_max,
                                               const ProbeProgressCallback& progress,
                                               const BedMeshProbeOptions& options = {});

// Pure-function helper, exposed for unit testing.
//
// Given the response lines from an `M115` query (Buddy firmware), return the
// expected G29 probe count for the detected printer model, or 0 if the model
// is unknown / no MACHINE_TYPE: line was present. Values come directly from
// Buddy's per-printer GRID_MAJOR_POINTS_X × GRID_MAJOR_POINTS_Y:
//
//   XL         12×12 = 144
//   iX          9×9  =  81
//   MINI        4×4  =  16
//   Core One / Core One L / MK4 / MK4S / MK3.5: 7×7 = 49
int expected_probe_count_from_m115_lines(const std::vector<std::string>& lines);

// Pure-function helper, exposed for unit testing.
//
// Parse a single firmware line and return the M73 percent value if present,
// or -1 if the line isn't an M73 progress report. Tolerates prefixes like
// "echo:" or "// " that the firmware sometimes prepends.
//
//   "M73 P56 R2"           → 56
//   "echo:M73 P100"        → 100
//   "// M73 P  7 R  3 C 1" →  7
//   "T:170.00 /170.00 ..." → -1
int parse_m73_progress(const std::string& line);

// Pure-function helper, exposed for unit testing.
//
// Given the response lines from an `M115` query, return the number of
// extruders reported via `EXTRUDER_COUNT:N`, or 0 if not present.
int extruder_count_from_m115_lines(const std::vector<std::string>& lines);

} // namespace Utils
} // namespace Slic3r

#endif
