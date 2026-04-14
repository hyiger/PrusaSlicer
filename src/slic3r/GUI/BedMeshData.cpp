///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "BedMeshData.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

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

// Parse one line as a row of whitespace/tab-separated floats.
// Returns a row on success, or empty vector on failure (non-numeric/empty line).
// NaN/Inf are preserved in the output — caller decides how to handle them.
static std::vector<float> parse_numeric_row(const std::string& in)
{
    std::string line = in;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) return {};

    std::istringstream iss(line);
    std::vector<float> row;
    float v;
    while (iss >> v) row.push_back(v);

    // Must be fully numeric (no trailing garbage like "|" or "19" row labels)
    iss.clear();
    std::string leftover;
    if (iss >> leftover) return {};
    if (row.size() < 2) return {};
    return row;
}

// Shared row-assembly from the collected raw rows (top-row = Y_max convention).
static BedMeshData finalize_mesh(std::vector<std::vector<float>>&& raw_rows,
                                 const Vec2d& probe_min,
                                 const Vec2d& probe_max)
{
    BedMeshData mesh;
    if (raw_rows.size() < 2) {
        mesh.status = BedMeshData::Status::Error;
        mesh.error_message = "Mesh has fewer than 2 rows";
        return mesh;
    }
    const size_t cols = raw_rows.front().size();
    for (const auto& r : raw_rows) {
        if (r.size() != cols) {
            mesh.status = BedMeshData::Status::Error;
            mesh.error_message = "Inconsistent row widths";
            return mesh;
        }
        for (float v : r) {
            if (std::isnan(v) || std::isinf(v)) {
                mesh.status = BedMeshData::Status::Error;
                mesh.error_message = "Mesh contains NaN/Inf (no mesh stored, or probing failed)";
                return mesh;
            }
        }
    }

    mesh.rows = raw_rows.size();
    mesh.cols = cols;
    mesh.z_values.resize(mesh.rows * mesh.cols);

    // Buddy firmware's CSV top row = Y_max. Renderer expects row 0 = probe_min.y,
    // so reverse the row order here.
    for (size_t r = 0; r < mesh.rows; ++r) {
        const auto& src = raw_rows[mesh.rows - 1 - r];
        std::copy(src.begin(), src.end(), mesh.z_values.begin() + r * mesh.cols);
    }

    const Vec2d probe_size = probe_max - probe_min;
    mesh.origin = probe_min;
    mesh.spacing = Vec2d(probe_size.x() / double(mesh.cols - 1),
                         probe_size.y() / double(mesh.rows - 1));

    mesh.recompute_range();
    mesh.status = BedMeshData::Status::Loaded;
    return mesh;
}

BedMeshData BedMeshData::load_from_csv(const std::string& path,
                                       const Vec2d& probe_min,
                                       const Vec2d& probe_max)
{
    BedMeshData mesh;
    std::ifstream f(path);
    if (!f.is_open()) {
        mesh.status = Status::Error;
        mesh.error_message = "Cannot open CSV: " + path;
        return mesh;
    }

    std::vector<std::vector<float>> raw_rows;
    std::string line;
    while (std::getline(f, line)) {
        std::vector<float> row = parse_numeric_row(line);
        if (!row.empty())
            raw_rows.push_back(std::move(row));
        else if (!raw_rows.empty())
            break; // end of numeric block
    }

    return finalize_mesh(std::move(raw_rows), probe_min, probe_max);
}

BedMeshData BedMeshData::parse_m420_output(const std::vector<std::string>& lines,
                                           const Vec2d& probe_min,
                                           const Vec2d& probe_max)
{
    BedMeshData mesh;

    // Locate the CSV header. Collect numeric rows immediately following it.
    bool in_csv_block = false;
    std::vector<std::vector<float>> raw_rows;
    for (const auto& raw : lines) {
        if (!in_csv_block) {
            if (raw.find("Bed Topography Report for CSV") != std::string::npos)
                in_csv_block = true;
            continue;
        }
        std::vector<float> row = parse_numeric_row(raw);
        if (!row.empty())
            raw_rows.push_back(std::move(row));
        else if (!raw_rows.empty())
            break;
    }

    if (!in_csv_block) {
        mesh.status = Status::Error;
        mesh.error_message = "No 'Bed Topography Report for CSV:' header in response";
        return mesh;
    }

    return finalize_mesh(std::move(raw_rows), probe_min, probe_max);
}

ColorRGBA z_deviation_to_color(float z, float z_min, float z_max, float z_ref)
{
    // Diverging colormap, symmetric around z_ref. Multi-stop ramp so modest
    // deviations still show a distinct color — a 2-color blue↔red gradient
    // compresses mid-magnitude points into dim tints and looks monochrome for
    // meshes dominated by one sign.
    const float max_abs = std::max(std::abs(z_min - z_ref), std::abs(z_max - z_ref));
    if (max_abs < 1e-6f)
        return ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);

    const float t = std::clamp((z - z_ref) / max_abs, -1.0f, 1.0f); // -1..+1

    // Below zero: dark blue -> blue -> cyan -> pale cyan -> white
    static const ColorRGBA stops_neg[5] = {
        { 0.05f, 0.05f, 0.45f, 1.0f }, // t = -1.0
        { 0.10f, 0.40f, 0.90f, 1.0f }, // t = -0.75
        { 0.25f, 0.80f, 0.95f, 1.0f }, // t = -0.50
        { 0.75f, 0.95f, 0.98f, 1.0f }, // t = -0.25
        { 1.00f, 1.00f, 1.00f, 1.0f }, // t =  0.0
    };
    // Above zero: white -> yellow -> orange -> red -> dark red
    static const ColorRGBA stops_pos[5] = {
        { 1.00f, 1.00f, 1.00f, 1.0f }, // t = 0.0
        { 1.00f, 0.95f, 0.35f, 1.0f }, // t = 0.25
        { 1.00f, 0.55f, 0.10f, 1.0f }, // t = 0.50
        { 0.90f, 0.15f, 0.05f, 1.0f }, // t = 0.75
        { 0.45f, 0.00f, 0.00f, 1.0f }, // t = 1.0
    };

    auto lerp = [](const ColorRGBA& a, const ColorRGBA& b, float u) {
        return ColorRGBA(a.r() + (b.r() - a.r()) * u,
                         a.g() + (b.g() - a.g()) * u,
                         a.b() + (b.b() - a.b()) * u,
                         1.0f);
    };
    auto sample = [&](const ColorRGBA* stops, float u) {
        // u in [0,1], 4 segments between 5 stops
        float s = u * 4.0f;
        int   i = std::min(3, std::max(0, int(s)));
        float f = s - float(i);
        return lerp(stops[i], stops[i + 1], f);
    };

    if (t >= 0.f)
        return sample(stops_pos, t);                 // 0..1
    return sample(stops_neg, 1.0f + t);              // t=-1 -> u=0, t=0 -> u=1
}

} // namespace GUI
} // namespace Slic3r
