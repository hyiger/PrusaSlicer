///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationRetractionPostProcessor_hpp_
#define slic3r_CalibrationRetractionPostProcessor_hpp_

#include <string>
#include <utility>
#include <vector>

namespace Slic3r {

// In-process post-processor for the Filament-Edition Retraction Calibration.
//
// Triggered via the standard `post_process` config when one of its entries
// starts with the `::builtin::retraction_calibration?...` prefix (see
// is_calibration_retraction_url). Rewrites slicer-side retract/recovery
// moves so that each Z band uses a per-band retraction distance, instead
// of the global retract_length the slicer baked in. Targets Buddy firmware,
// which does not implement M207/G10/G11 (so the firmware-retraction path
// the dialog used previously is a no-op there).
//
// URL format:
//     ::builtin::retraction_calibration?base=<mm>&levels=<z>:<mm>,<z>:<mm>,...
// where each `<z>:<mm>` pair is `z_top_exclusive:retract_mm` ascending by z_top.

bool is_calibration_retraction_url(const std::string &script);

// Run the in-process post-processor against `gcode_path` using the parameters
// embedded in `url`. Returns true on success. Throws Slic3r::RuntimeError on
// malformed URL or unreadable file.
bool run_calibration_retraction_post_processor(const std::string &url, const std::string &gcode_path);

// Build a `::builtin::retraction_calibration?...` URL from a level table.
std::string make_calibration_retraction_url(double base_retract,
                                            const std::vector<std::pair<double, double>> &levels);

} // namespace Slic3r

#endif // slic3r_CalibrationRetractionPostProcessor_hpp_
