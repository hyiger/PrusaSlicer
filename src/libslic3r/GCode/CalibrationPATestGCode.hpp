///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationPATestGCode_hpp_
#define slic3r_CalibrationPATestGCode_hpp_

#include <string>

namespace Slic3r {

// Which flat pressure-advance test to generate. Both vary PA *horizontally*
// (one value per band across the bed) within a single printed layer, which
// the per-Z chevron-tower path cannot express — hence direct G-code.
enum class PATestKind { Line, Pattern };

// Firmware-specific pressure-advance command family.
enum class PATestCommand { M572, M900, Klipper };

struct PATestParams
{
    PATestKind    kind    = PATestKind::Line;
    PATestCommand command = PATestCommand::M572;

    // PA sweep — one band per value, front (low Y) = start_pa.
    double start_pa = 0.0;
    double end_pa   = 0.10;
    double step_pa  = 0.005;

    // Extrusion geometry / flow.
    double nozzle_diameter      = 0.4;   // mm
    double line_width           = 0.0;   // mm; <= 0 ⇒ 1.125 × nozzle_diameter
    double layer_height         = 0.2;   // mm
    double filament_diameter    = 1.75;  // mm
    double extrusion_multiplier = 1.0;

    // Speeds (mm/s).
    double slow_speed   = 30.0;   // line lead-in/out; reveals the PA transition
    double fast_speed   = 100.0;  // line middle / pattern corners
    double travel_speed = 120.0;
    double anchor_speed = 20.0;   // anchor frame

    // Bed size (mm); the test is centered on the bed.
    double bed_size_x = 250.0;
    double bed_size_y = 220.0;

    // Retraction between disjoint bands (mm).
    double retract_length = 0.8;

    // Pre-substituted printer start / end G-code (homing, leveling, priming,
    // temperatures, cooldown). Passed through verbatim so the test inherits
    // the printer's real first-layer setup. When empty, a minimal built-in
    // sequence is emitted (homes + heats only — no mesh leveling).
    std::string start_gcode;
    std::string end_gcode;

    // Only used by the built-in fallback start/end sequence.
    int nozzle_temp = 215;
    int bed_temp    = 60;
};

// Number of PA bands the sweep produces (always >= 1).
int pa_test_band_count(const PATestParams& p);

// Effective extrusion width (resolves line_width <= 0 to 1.125 × nozzle).
double pa_test_line_width(const PATestParams& p);

// Filament mm of extrusion per mm of travel for the given params, using the
// same rounded-rectangle extrudate model as PrusaSlicer's Flow.
double pa_test_e_per_mm(const PATestParams& p);

// Produce a complete, self-contained .gcode for the chosen flat PA test.
std::string generate_pa_test_gcode(const PATestParams& p);

} // namespace Slic3r

#endif // slic3r_CalibrationPATestGCode_hpp_
