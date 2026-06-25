///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationPAPostProcessor_hpp_
#define slic3r_CalibrationPAPostProcessor_hpp_

#include <string>
#include <utility>
#include <vector>

namespace Slic3r {

// In-process post-processor for the flat (line / pattern) Pressure Advance
// calibration. Unlike the chevron *tower* (which changes PA per Z layer), the
// flat test prints one small chevron band per PA value, side by side on a
// single layer. PA cannot vary within a layer through the slice pipeline, so —
// exactly like the YOLO Flow Rate tool — each band is a separate object and
// this post-processor INJECTS the firmware PA command at each object's start,
// keyed on the object-label name. Everything else (temperatures, leveling,
// flow, acceleration, axis, etc.) is produced by the real exporter, so it
// matches a normal print of the same printer/filament.
//
// Triggered via the standard `post_process` config when an entry starts with
// `::builtin::pa_calibration?...`. It rewrites ONLY a G-code carrying the
// job-scoped calibration marker (calibration_pa_marker) and is a pass-through
// no-op otherwise. The dialog forces OctoPrint object labeling, so the boundary
// comments ("; printing object <name>" / "; stop printing object <name>") are
// emitted on every G-code flavor.
//
// URL format:
//     ::builtin::pa_calibration?cmd=<m572|m900|klipper>&base=<pa>&objects=<name>:<pa>,...
// <name> is the object-label name (the PA value's text, e.g. "0.020"); <pa> is
// the pressure-advance value for that band; <base> is restored after each band.

enum GCodeFlavor : unsigned char;   // defined in libslic3r/PrintConfig.hpp

enum class PACalibrationCommand { M572, M900, Klipper };

// Select the firmware pressure-advance / linear-advance command for a printer, given
// its g-code flavor and printer_notes. Klipper -> SET_PRESSURE_ADVANCE; RepRapFirmware
// /Duet -> M572. For Marlin-flavored printers (which includes ALL Prusa printers) the
// choice depends on the firmware GENERATION, not the flavor: Prusa's Buddy input-shaper
// line uses M572 S (pressure advance), while older Prusa and generic Marlin use M900 K
// (linear advance). Detected from the same printer_notes markers Prusa's own
// start_filament_gcode keys on -- the printer MODEL name is unreliable (the MK3.9 is
// flagged PRINTER_MODEL_MK4IS in its notes, not "MK3.9").
PACalibrationCommand select_pa_command(GCodeFlavor flavor, const std::string &printer_notes);

bool is_calibration_pa_url(const std::string &script);

inline constexpr const char *calibration_pa_marker()
{
    return "PRUSASLICER_PA_CALIBRATION";
}

// Run the post-processor against `gcode_path`. Returns true if the file was
// rewritten, false if left untouched (no marker — i.e. not a PA calibration
// print). Throws on a malformed URL, an unreadable file, or a marker-carrying
// binary G-code.
bool run_calibration_pa_post_processor(const std::string &url, const std::string &gcode_path);

// Build a `::builtin::pa_calibration?...` URL. PA values are formatted
// locale-invariantly ('.' decimals).
std::string make_calibration_pa_url(PACalibrationCommand cmd, double base_pa,
                                    const std::vector<std::pair<std::string, double>> &objects);

} // namespace Slic3r

#endif // slic3r_CalibrationPAPostProcessor_hpp_
