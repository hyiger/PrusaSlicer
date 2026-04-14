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

    // Reference point for the color map.
    //   Zero: white at Z=0, red above / blue below — shows absolute deviation
    //         from the nominal bed plane. Useful for "how much compensation?"
    //   Mean: white at the mesh mean — shows relative flatness. Useful for
    //         diagnosing warp/bowl/tilt independent of Z-offset calibration.
    enum class Reference : unsigned char { Zero, Mean };

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

    // Load a mesh from a whitespace/tab-separated CSV file as emitted by
    // Prusa Buddy firmware's `M420 V1 T1` "Bed Topography Report for CSV:" block.
    // The CSV's first row corresponds to Y_max (top of the bed topography report),
    // so rows are reversed on load so row 0 = probe_min.y (front of bed).
    // probe_min/probe_max define the XY extent of the probe grid in mm.
    // Returns a mesh with status=Loaded on success, status=Error otherwise.
    static BedMeshData load_from_csv(const std::string& path,
                                     const Vec2d& probe_min,
                                     const Vec2d& probe_max);

    // Parse a sequence of lines (e.g. the response to `M420 V1 T1` over serial)
    // and extract the mesh. Looks for a "Bed Topography Report for CSV:" header
    // and reads the consecutive numeric rows that follow. Same row-reversal and
    // validity checks as load_from_csv.
    static BedMeshData parse_m420_output(const std::vector<std::string>& lines,
                                         const Vec2d& probe_min,
                                         const Vec2d& probe_max);

    // Write this mesh to a tab-separated CSV compatible with load_from_csv
    // and with Buddy firmware's "Bed Topography Report for CSV" format.
    // Row 0 in the file corresponds to Y_max (bed back), matching the
    // on-the-wire convention — load_from_csv will reverse on reload.
    // Returns empty string on success, or a human-readable error message.
    std::string save_to_csv(const std::string& path) const;

    // Element-wise subtraction for compare mode. Both meshes must have the
    // same rows/cols (and both be Loaded); on mismatch, returns a mesh with
    // status=Error. XY origin/spacing are copied from *this.
    BedMeshData subtract(const BedMeshData& rhs) const;
};

// Map a Z value to a heatmap color via a diverging ramp centered at z_ref.
// Full saturation is reached at whichever side of z_ref is more extreme
// (i.e. the mapping is symmetric about z_ref in color space, and clamped at
// max(|z_min - z_ref|, |z_max - z_ref|)).
ColorRGBA z_deviation_to_color(float z, float z_min, float z_max, float z_ref = 0.f);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_BedMeshData_hpp_
