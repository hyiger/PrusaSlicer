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
    double z_offset             = 0.0;   // mm; added to the printed Z (profile z_offset)
    double filament_diameter    = 1.75;  // mm
    double extrusion_multiplier = 1.0;
    bool   volumetric_e         = false; // profile use_volumetric_e (E in mm^3)
    int    extruder_count       = 1;     // emit an initial T0 when > 1

    // Speeds (mm/s).
    double slow_speed   = 30.0;   // line lead-in/out; reveals the PA transition
    double fast_speed   = 100.0;  // line middle / pattern corners
    double travel_speed = 120.0;
    double anchor_speed = 20.0;   // anchor frame

    // Printable bed extent (mm); the test is centered within [min, max].
    // bed_size_* is the maximum corner, bed_min_* the minimum — non-zero for
    // delta / origin-offset beds whose bed_shape does not start at (0,0).
    double bed_min_x  = 0.0;
    double bed_min_y  = 0.0;
    double bed_size_x = 250.0;
    double bed_size_y = 220.0;

    // Retraction between disjoint bands (mm).
    double retract_length = 0.8;

    // Extrusion axis letter (profile's extrusion_axis, usually "E"; some
    // printers use "A"). Used for every deposition/retract/G92 move.
    std::string extrusion_axis = "E";

    // The printer profile's extrusion mode. The test body always emits
    // relative E (M83); when the profile is absolute, M82 is restored before
    // the (profile-authored) end G-code so its cleanup moves run correctly.
    bool relative_e = true;

    // Pre-substituted printer start / end G-code (homing, leveling, priming,
    // temperatures, cooldown). Passed through verbatim so the test inherits
    // the printer's real first-layer setup. When empty, a minimal built-in
    // sequence is emitted (homes + heats only — no mesh leveling).
    std::string start_gcode;
    std::string end_gcode;

    // First-layer temperatures (°C). Used by the built-in start sequence and,
    // when autoemit_temps is set, to emit M104/M140/M109/M190 around a custom
    // start G-code that relies on PrusaSlicer's auto temperature emission.
    int  nozzle_temp    = 215;
    int  bed_temp       = 60;
    bool autoemit_temps = true;   // printer's autoemit_temperature_commands
};

// Number of PA bands the sweep produces (always >= 1).
int pa_test_band_count(const PATestParams& p);

// Effective extrusion width (resolves line_width <= 0 to 1.125 × nozzle).
double pa_test_line_width(const PATestParams& p);

// Filament mm of extrusion per mm of travel for the given params, using the
// same rounded-rectangle extrudate model as PrusaSlicer's Flow.
double pa_test_e_per_mm(const PATestParams& p);

// Minimum Y extent (mm) the test occupies at its tightest legal band spacing.
double pa_test_required_span_y(const PATestParams& p);

// Whether the sweep's bands fit within the bed's Y extent (with margin). When
// false the caller must reject the sweep — the generator would otherwise emit
// bands that run off the bed.
bool pa_test_fits_bed(const PATestParams& p);

// XY bounding box (mm) of the deposition the generator will emit. Lets the
// caller validate placement against a non-rectangular bed polygon and seed the
// printer's start-G-code geometry placeholders (first_layer_print_min/max).
struct PATestFootprint { double x_min, y_min, x_max, y_max; };
PATestFootprint pa_test_footprint(const PATestParams& p);

// Produce a complete, self-contained .gcode for the chosen flat PA test.
std::string generate_pa_test_gcode(const PATestParams& p);

} // namespace Slic3r

#endif // slic3r_CalibrationPATestGCode_hpp_
