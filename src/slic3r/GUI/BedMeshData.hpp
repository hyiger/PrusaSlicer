///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_BedMeshData_hpp_
#define slic3r_BedMeshData_hpp_

#include "libslic3r/Point.hpp"
#include "libslic3r/Color.hpp"

#include <vector>
#include <string>
#include <cmath>
#include <numeric>

namespace Slic3r {
namespace GUI {

struct BedMeshData
{
    enum class Status : unsigned char { Empty, Fetching, Loaded, Error };

    size_t rows = 0;
    size_t cols = 0;
    std::vector<float> z_values; // row-major, size = rows * cols
    Vec2d origin{0.0, 0.0};     // XY of first probe point (mm)
    Vec2d spacing{0.0, 0.0};    // XY step between probe points (mm)
    float z_min = 0.f;
    float z_max = 0.f;
    Status status = Status::Empty;
    std::string error_message;

    bool is_valid() const { return status == Status::Loaded && rows > 1 && cols > 1 && z_values.size() == rows * cols; }

    float get(size_t row, size_t col) const { return z_values[row * cols + col]; }

    void recompute_range()
    {
        if (z_values.empty()) {
            z_min = z_max = 0.f;
            return;
        }
        z_min = *std::min_element(z_values.begin(), z_values.end());
        z_max = *std::max_element(z_values.begin(), z_values.end());
    }

    float mean() const
    {
        if (z_values.empty()) return 0.f;
        return std::accumulate(z_values.begin(), z_values.end(), 0.f) / float(z_values.size());
    }

    float std_dev() const
    {
        if (z_values.size() < 2) return 0.f;
        float m = mean();
        float sum_sq = 0.f;
        for (float z : z_values)
            sum_sq += (z - m) * (z - m);
        return std::sqrt(sum_sq / float(z_values.size() - 1));
    }

    // Generate a mock 7x7 mesh for development/testing.
    // Simulates a gentle bowl shape typical of a slightly warped bed.
    // bed_min/bed_max define the printable area in mm.
    static BedMeshData create_mock(const Vec2d& bed_min, const Vec2d& bed_max);
};

// Map a Z deviation to a heatmap color: blue (low) -> green (mid) -> red (high).
ColorRGBA z_deviation_to_color(float z, float z_min, float z_max);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_BedMeshData_hpp_
