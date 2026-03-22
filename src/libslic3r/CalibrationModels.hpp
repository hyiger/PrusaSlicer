///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationModels_hpp_
#define slic3r_CalibrationModels_hpp_

#include "admesh/stl.h"

namespace Slic3r {

// Generate a temperature calibration tower mesh.
// The tower has a 1mm base plate and num_tiers stacked 10mm tiers,
// each with overhang features, holes, cones, cutout, protrusion,
// and engraved temperature labels on the back face.
// start_temp: temperature of the first (bottom) tier
// temp_step: temperature decrease per tier (positive value)
indexed_triangle_set make_temp_tower(int num_tiers, int start_temp, int temp_step);

// Generate a serpentine (E-shaped) flow-rate calibration specimen.
// When printed in spiral vase mode, produces a long continuous perimeter
// path ideal for sustained extrusion rate testing.
indexed_triangle_set make_flow_specimen(
    int    num_levels,
    double level_height  = 1.0,
    double width         = 170.0,
    double arm_thickness = 20.0,
    double gap_width     = 20.0,
    int    num_arms      = 3
);

/// Generate a chevron (V-shape) pattern for pressure advance calibration.
/// The model contains num_patterns nested chevrons inside a rectangular
/// frame, extruded to num_layers × layer_height.  Each layer is assigned
/// a different PA value via per-layer custom G-code.
/// Returns the mesh centred at XY origin.
indexed_triangle_set make_pa_pattern(
    int    num_patterns,
    int    num_layers     = 4,
    double layer_height   = 0.2,
    double corner_angle   = 90.0,    // degrees
    double arm_length     = 40.0,    // mm
    double wall_thickness = 1.6,     // mm
    double pattern_spacing = 1.6     // mm, edge-to-edge gap between arms
);

} // namespace Slic3r

#endif // slic3r_CalibrationModels_hpp_
