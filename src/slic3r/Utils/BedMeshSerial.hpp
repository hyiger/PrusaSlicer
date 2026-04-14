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

// Run a full bed probing cycle on the printer: G28 (home), heat nozzle to
// nozzle_temp_c (probe-safe), G29 (probe grid), cool nozzle, then fetch the
// resulting mesh via M420 V1 T1. Typically takes 3–5 minutes.
//
// progress is called from this function's thread whenever the firmware emits
// a meaningful line. cancel_requested, if non-null, is polled between reads;
// on true, the function attempts to stop (but G29 itself cannot be safely
// interrupted, so cancellation only takes effect between phases).
BedMeshFetchResult probe_bed_mesh_from_printer(const Vec2d& probe_min,
                                               const Vec2d& probe_max,
                                               const ProbeProgressCallback& progress,
                                               std::atomic<bool>* cancel_requested = nullptr,
                                               const std::string& explicit_port = {},
                                               int nozzle_temp_c = 170);

} // namespace Utils
} // namespace Slic3r

#endif
