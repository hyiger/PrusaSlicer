///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "BedMeshData.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {
namespace GUI {

BedMeshData BedMeshData::create_mock(const Vec2d& bed_min, const Vec2d& bed_max)
{
    BedMeshData mesh;
    mesh.rows = 7;
    mesh.cols = 7;

    const Vec2d bed_size = bed_max - bed_min;
    // Inset the probe grid slightly from bed edges (10mm margin)
    const double margin = 10.0;
    const Vec2d probe_min = bed_min + Vec2d(margin, margin);
    const Vec2d probe_max = bed_max - Vec2d(margin, margin);
    const Vec2d probe_size = probe_max - probe_min;

    mesh.origin = probe_min;
    mesh.spacing = Vec2d(probe_size.x() / double(mesh.cols - 1),
                         probe_size.y() / double(mesh.rows - 1));

    mesh.z_values.resize(mesh.rows * mesh.cols);

    // Simulate a realistic bed mesh: slight saddle shape with a low front-left
    // corner and high back-right, plus local waviness. Centered around zero
    // with values typically in the -0.15 to +0.15mm range.
    for (size_t r = 0; r < mesh.rows; ++r) {
        for (size_t c = 0; c < mesh.cols; ++c) {
            double nx = double(c) / double(mesh.cols - 1); // 0..1
            double ny = double(r) / double(mesh.rows - 1); // 0..1

            // Tilt: bed slopes slightly from front-left (-) to back-right (+)
            double tilt = 0.08 * (nx + ny - 1.0);

            // Saddle: high on two diagonal corners, low on the other two
            double saddle = 0.06 * (2.0 * nx - 1.0) * (2.0 * ny - 1.0);

            // Bowl: slight center depression
            double dx = nx - 0.5;
            double dy = ny - 0.5;
            double bowl = -0.04 * (1.0 - 4.0 * (dx * dx + dy * dy));

            // Local waviness to simulate surface imperfections
            double wave = 0.02 * std::sin(nx * 9.42) * std::cos(ny * 6.28)
                        + 0.015 * std::cos(nx * 12.57 + ny * 3.14);

            mesh.z_values[r * mesh.cols + c] = float(tilt + saddle + bowl + wave);
        }
    }

    mesh.recompute_range();
    mesh.status = Status::Loaded;
    return mesh;
}

ColorRGBA z_deviation_to_color(float z, float z_min, float z_max)
{
    // Symmetric around zero: red = above 0, blue = below 0.
    // Intensity scales with magnitude. White at exactly zero.
    float max_abs = std::max(std::abs(z_min), std::abs(z_max));
    if (max_abs < 1e-6f)
        return ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f); // all flat = white

    float t = std::clamp(z / max_abs, -1.0f, 1.0f); // -1..+1

    if (t >= 0.f) {
        // Above zero: white(0) -> red(1)
        return ColorRGBA(1.0f, 1.0f - t, 1.0f - t, 1.0f);
    } else {
        // Below zero: white(0) -> blue(-1)
        float a = -t;
        return ColorRGBA(1.0f - a, 1.0f - a, 1.0f, 1.0f);
    }
}

} // namespace GUI
} // namespace Slic3r
