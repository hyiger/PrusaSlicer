///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "slic3r/GUI/BedMeshData.hpp"
#include "slic3r/Utils/BedMeshSerial.hpp"

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::GUI;
using Catch::Matchers::WithinAbs;

// ----------------------------------------------------------------------------
// BedMeshData — basic accessors / statistics / validity
// ----------------------------------------------------------------------------

TEST_CASE("BedMeshData: default-constructed mesh is invalid", "[bedmesh]") {
    BedMeshData m;
    REQUIRE(m.status == BedMeshData::Status::Empty);
    REQUIRE(!m.is_valid());
    REQUIRE(m.rows == 0);
    REQUIRE(m.cols == 0);
}

TEST_CASE("BedMeshData::is_valid requires Loaded status and >=2x2 grid", "[bedmesh]") {
    BedMeshData m;
    m.rows = 3; m.cols = 3;
    m.z_values = std::vector<float>(9, 0.0f);

    // status still Empty → invalid
    REQUIRE(!m.is_valid());

    m.status = BedMeshData::Status::Loaded;
    REQUIRE(m.is_valid());

    // 1x1 rejected
    m.rows = 1; m.cols = 1; m.z_values = { 0.f };
    REQUIRE(!m.is_valid());

    // size mismatch rejected
    m.rows = 3; m.cols = 3; m.z_values.resize(5);
    REQUIRE(!m.is_valid());
}

TEST_CASE("BedMeshData::get returns row-major elements", "[bedmesh]") {
    BedMeshData m;
    m.rows = 2; m.cols = 3;
    m.z_values = { 0.0f, 1.0f, 2.0f, 10.0f, 11.0f, 12.0f };
    REQUIRE(m.get(0, 0) == 0.0f);
    REQUIRE(m.get(0, 2) == 2.0f);
    REQUIRE(m.get(1, 0) == 10.0f);
    REQUIRE(m.get(1, 2) == 12.0f);
}

TEST_CASE("BedMeshData::recompute_range finds min and max", "[bedmesh]") {
    BedMeshData m;
    m.z_values = { -0.5f, 0.0f, 0.25f, -0.1f, 0.4f };
    m.recompute_range();
    REQUIRE_THAT(m.z_min, WithinAbs(-0.5f, 1e-6f));
    REQUIRE_THAT(m.z_max, WithinAbs( 0.4f, 1e-6f));

    // Empty mesh → zeros, no crash
    BedMeshData empty;
    empty.recompute_range();
    REQUIRE(empty.z_min == 0.0f);
    REQUIRE(empty.z_max == 0.0f);
}

TEST_CASE("BedMeshData::mean computes arithmetic mean", "[bedmesh]") {
    BedMeshData m;
    m.z_values = { 1.0f, 2.0f, 3.0f, 4.0f };
    REQUIRE_THAT(m.mean(), WithinAbs(2.5f, 1e-6f));

    BedMeshData empty;
    REQUIRE(empty.mean() == 0.0f);
}

TEST_CASE("BedMeshData::std_dev computes sample standard deviation", "[bedmesh]") {
    BedMeshData m;
    // Values 2,4,4,4,5,5,7,9 have mean 5, population stddev 2, sample stddev ≈ 2.138
    m.z_values = { 2.f, 4.f, 4.f, 4.f, 5.f, 5.f, 7.f, 9.f };
    REQUIRE_THAT(m.std_dev(), WithinAbs(2.1380899f, 1e-4f));

    // Constant → zero
    BedMeshData flat;
    flat.z_values = { 0.3f, 0.3f, 0.3f, 0.3f };
    REQUIRE_THAT(flat.std_dev(), WithinAbs(0.0f, 1e-6f));

    // Fewer than 2 samples → zero, no divide-by-zero
    BedMeshData one;
    one.z_values = { 1.0f };
    REQUIRE(one.std_dev() == 0.0f);

    BedMeshData empty;
    REQUIRE(empty.std_dev() == 0.0f);
}

// ----------------------------------------------------------------------------
// BedMeshData::create_mock
// ----------------------------------------------------------------------------

TEST_CASE("BedMeshData::create_mock produces a 7x7 grid centered around zero",
          "[bedmesh][mock]") {
    const Vec2d bed_min(0.0, 0.0);
    const Vec2d bed_max(250.0, 220.0);
    BedMeshData m = BedMeshData::create_mock(bed_min, bed_max);

    REQUIRE(m.is_valid());
    REQUIRE(m.rows == 7);
    REQUIRE(m.cols == 7);
    REQUIRE(m.z_values.size() == 49);

    // Probe grid inset by 10mm margin as documented.
    REQUIRE_THAT(m.origin.x(), WithinAbs(10.0, 1e-9));
    REQUIRE_THAT(m.origin.y(), WithinAbs(10.0, 1e-9));
    // spacing = (230 / 6, 200 / 6)
    REQUIRE_THAT(m.spacing.x(), WithinAbs(230.0 / 6.0, 1e-9));
    REQUIRE_THAT(m.spacing.y(), WithinAbs(200.0 / 6.0, 1e-9));

    // Values should be within realistic bed-deviation range and cross zero.
    REQUIRE(m.z_min < 0.0f);
    REQUIRE(m.z_max > 0.0f);
    REQUIRE(m.z_max - m.z_min < 0.5f);
    for (float z : m.z_values) {
        REQUIRE(std::isfinite(z));
        REQUIRE(std::abs(z) < 0.3f);
    }
}

// ----------------------------------------------------------------------------
// BedMeshData::parse_m420_output
// ----------------------------------------------------------------------------

TEST_CASE("parse_m420_output parses a Buddy firmware CSV block",
          "[bedmesh][parse]") {
    std::vector<std::string> lines = {
        "echo:Bed Leveling ON",
        "Bed Topography Report for CSV:",
        "0.10\t0.15\t0.20",
        "0.05\t0.00\t-0.05",
        "-0.10\t-0.05\t0.00",
        "ok"
    };
    // Nominal 100x100 probe extent, first row corresponds to Y_max so the
    // loader should reverse: row 0 of output = last raw row.
    BedMeshData m = BedMeshData::parse_m420_output(lines,
        Vec2d(0.0, 0.0), Vec2d(100.0, 100.0));

    REQUIRE(m.is_valid());
    REQUIRE(m.rows == 3);
    REQUIRE(m.cols == 3);
    // Row 0 == the *last* input row (Y_max convention is reversed).
    REQUIRE_THAT(m.get(0, 0), WithinAbs(-0.10f, 1e-6f));
    REQUIRE_THAT(m.get(0, 2), WithinAbs( 0.00f, 1e-6f));
    // Row 2 == the *first* input row.
    REQUIRE_THAT(m.get(2, 0), WithinAbs( 0.10f, 1e-6f));
    REQUIRE_THAT(m.get(2, 2), WithinAbs( 0.20f, 1e-6f));

    REQUIRE_THAT(m.z_min, WithinAbs(-0.10f, 1e-6f));
    REQUIRE_THAT(m.z_max, WithinAbs( 0.20f, 1e-6f));
    REQUIRE_THAT(m.spacing.x(), WithinAbs(50.0, 1e-9));
    REQUIRE_THAT(m.spacing.y(), WithinAbs(50.0, 1e-9));
}

TEST_CASE("parse_m420_output errors when the CSV header is missing",
          "[bedmesh][parse]") {
    std::vector<std::string> lines = {
        "echo:Bed Leveling OFF",
        "0.1 0.2",
        "0.3 0.4",
        "ok"
    };
    BedMeshData m = BedMeshData::parse_m420_output(lines,
        Vec2d(0, 0), Vec2d(10, 10));
    REQUIRE(!m.is_valid());
    REQUIRE(m.status == BedMeshData::Status::Error);
    REQUIRE(m.error_message.find("Bed Topography Report") != std::string::npos);
}

TEST_CASE("parse_m420_output rejects a mesh containing nan/inf tokens",
          "[bedmesh][parse]") {
    // "nan" / "inf" text in a row should never produce a valid mesh.
    // The exact error depends on the stdlib's float-parsing: glibc parses
    // "nan" → NaN (triggers the NaN/Inf guard in finalize_mesh), whereas
    // Apple's libc++ fails the parse (so the line is treated as end-of-CSV
    // and we hit the "fewer than 2 rows" guard instead). Either way, the
    // mesh must be flagged invalid so a bad probe never silently loads.
    std::vector<std::string> lines = {
        "Bed Topography Report for CSV:",
        "0.1 0.2",
        "nan 0.4",
        "ok"
    };
    BedMeshData m = BedMeshData::parse_m420_output(lines,
        Vec2d(0, 0), Vec2d(10, 10));
    REQUIRE(!m.is_valid());
    REQUIRE(m.status == BedMeshData::Status::Error);
    REQUIRE(!m.error_message.empty());
}

TEST_CASE("parse_m420_output rejects inconsistent row widths",
          "[bedmesh][parse]") {
    std::vector<std::string> lines = {
        "Bed Topography Report for CSV:",
        "0.1 0.2 0.3",
        "0.4 0.5",
        "ok"
    };
    BedMeshData m = BedMeshData::parse_m420_output(lines,
        Vec2d(0, 0), Vec2d(10, 10));
    REQUIRE(!m.is_valid());
    REQUIRE(m.status == BedMeshData::Status::Error);
    REQUIRE(m.error_message.find("row") != std::string::npos);
}

TEST_CASE("parse_m420_output rejects fewer than 2 rows",
          "[bedmesh][parse]") {
    std::vector<std::string> lines = {
        "Bed Topography Report for CSV:",
        "0.1 0.2 0.3",
        "ok"
    };
    BedMeshData m = BedMeshData::parse_m420_output(lines,
        Vec2d(0, 0), Vec2d(10, 10));
    REQUIRE(!m.is_valid());
    REQUIRE(m.status == BedMeshData::Status::Error);
}

TEST_CASE("parse_m420_output tolerates CR line endings and trailing junk",
          "[bedmesh][parse]") {
    std::vector<std::string> lines = {
        "Bed Topography Report for CSV:\r",
        "0.10\t0.20\r",
        "0.30\t0.40\r",
        "ok\r"
    };
    BedMeshData m = BedMeshData::parse_m420_output(lines,
        Vec2d(0, 0), Vec2d(10, 10));
    REQUIRE(m.is_valid());
    REQUIRE(m.rows == 2);
    REQUIRE(m.cols == 2);
}

// ----------------------------------------------------------------------------
// BedMeshData::load_from_csv
// ----------------------------------------------------------------------------

TEST_CASE("load_from_csv returns an error for a missing file",
          "[bedmesh][csv]") {
    BedMeshData m = BedMeshData::load_from_csv(
        "/tmp/__bedmesh_does_not_exist__.csv",
        Vec2d(0, 0), Vec2d(10, 10));
    REQUIRE(!m.is_valid());
    REQUIRE(m.status == BedMeshData::Status::Error);
    REQUIRE(m.error_message.find("Cannot open") != std::string::npos);
}

TEST_CASE("load_from_csv reads a well-formed mesh and reverses Y",
          "[bedmesh][csv]") {
    namespace bfs = boost::filesystem;
    bfs::path tmp = bfs::unique_path("%%%%-%%%%-bedmesh.csv");
    tmp = bfs::temp_directory_path() / tmp;
    {
        bfs::ofstream os(tmp);
        os << "0.10\t0.20\n"
              "0.30\t0.40\n";
    }

    BedMeshData m = BedMeshData::load_from_csv(tmp.string(),
        Vec2d(0, 0), Vec2d(100, 100));
    bfs::remove(tmp);

    REQUIRE(m.is_valid());
    REQUIRE(m.rows == 2);
    REQUIRE(m.cols == 2);
    // Y reversed: row 0 in memory = last line in file
    REQUIRE_THAT(m.get(0, 0), WithinAbs(0.30f, 1e-6f));
    REQUIRE_THAT(m.get(1, 0), WithinAbs(0.10f, 1e-6f));
}

// ----------------------------------------------------------------------------
// z_deviation_to_color
// ----------------------------------------------------------------------------

TEST_CASE("z_deviation_to_color returns white on a flat mesh",
          "[bedmesh][color]") {
    ColorRGBA c = z_deviation_to_color(0.0f, 0.0f, 0.0f);
    REQUIRE(c.r() == 1.0f);
    REQUIRE(c.g() == 1.0f);
    REQUIRE(c.b() == 1.0f);
}

TEST_CASE("z_deviation_to_color is symmetric about z_ref",
          "[bedmesh][color]") {
    // Reference = 0, range [-0.2, 0.2]
    ColorRGBA center = z_deviation_to_color(0.0f, -0.2f, 0.2f);
    REQUIRE_THAT(center.r(), WithinAbs(1.0f, 1e-4f));
    REQUIRE_THAT(center.g(), WithinAbs(1.0f, 1e-4f));
    REQUIRE_THAT(center.b(), WithinAbs(1.0f, 1e-4f));

    ColorRGBA high = z_deviation_to_color( 0.2f, -0.2f, 0.2f);
    ColorRGBA low  = z_deviation_to_color(-0.2f, -0.2f, 0.2f);

    // Top of ramp → dark red; bottom → dark blue.
    // Red channel: high > low. Blue channel: low > high.
    REQUIRE(high.r() > high.b());
    REQUIRE(low.b()  > low.r());
    REQUIRE(high.r() > low.r());
    REQUIRE(low.b()  > high.b());
}

TEST_CASE("z_deviation_to_color supports a non-zero reference",
          "[bedmesh][color]") {
    // Mesh ranges 0.1..0.5, reference = mean = 0.3
    // At z = z_ref, result must be white regardless of range.
    ColorRGBA at_ref = z_deviation_to_color(0.3f, 0.1f, 0.5f, 0.3f);
    REQUIRE_THAT(at_ref.r(), WithinAbs(1.0f, 1e-4f));
    REQUIRE_THAT(at_ref.g(), WithinAbs(1.0f, 1e-4f));
    REQUIRE_THAT(at_ref.b(), WithinAbs(1.0f, 1e-4f));

    ColorRGBA below_ref = z_deviation_to_color(0.1f, 0.1f, 0.5f, 0.3f);
    REQUIRE(below_ref.b() > below_ref.r()); // blue-ish
    ColorRGBA above_ref = z_deviation_to_color(0.5f, 0.1f, 0.5f, 0.3f);
    REQUIRE(above_ref.r() > above_ref.b()); // red-ish
}

TEST_CASE("z_deviation_to_color clamps values outside the range",
          "[bedmesh][color]") {
    ColorRGBA way_high = z_deviation_to_color( 10.0f, -0.1f, 0.1f);
    ColorRGBA at_max   = z_deviation_to_color(  0.1f, -0.1f, 0.1f);
    // Both should land at the top stop (dark red) → identical.
    REQUIRE_THAT(way_high.r(), WithinAbs(at_max.r(), 1e-6f));
    REQUIRE_THAT(way_high.g(), WithinAbs(at_max.g(), 1e-6f));
    REQUIRE_THAT(way_high.b(), WithinAbs(at_max.b(), 1e-6f));

    ColorRGBA way_low = z_deviation_to_color(-10.0f, -0.1f, 0.1f);
    ColorRGBA at_min  = z_deviation_to_color( -0.1f, -0.1f, 0.1f);
    REQUIRE_THAT(way_low.r(), WithinAbs(at_min.r(), 1e-6f));
    REQUIRE_THAT(way_low.g(), WithinAbs(at_min.g(), 1e-6f));
    REQUIRE_THAT(way_low.b(), WithinAbs(at_min.b(), 1e-6f));
}

// ----------------------------------------------------------------------------
// expected_probe_count_from_m115_lines
// ----------------------------------------------------------------------------

TEST_CASE("expected_probe_count returns 49 for Core One and MK4 family",
          "[bedmesh][probe_count]") {
    using Utils::expected_probe_count_from_m115_lines;

    // Core One
    REQUIRE(expected_probe_count_from_m115_lines({
        "FIRMWARE_NAME:Prusa-Firmware-Buddy 6.5.3+12780 "
        "PROTOCOL_VERSION:1.0 MACHINE_TYPE:Prusa-Core-One EXTRUDER_COUNT:1",
        "ok"
    }) == 49);

    // Core One L (hypothetical — same machine-type string per current Buddy
    // firmware); the "Core-One" substring still matches.
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:Prusa-Core-One-L"
    }) == 49);

    // CoreOne spelling variant (no dash)
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:Prusa-CoreOne"
    }) == 49);

    // MK4
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:Prusa-MK4"
    }) == 49);

    // MK4S
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:Prusa-MK4S"
    }) == 49);

    // MK3.5
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:Prusa-MK3.5"
    }) == 49);
}

TEST_CASE("expected_probe_count returns 144 for XL",
          "[bedmesh][probe_count]") {
    using Utils::expected_probe_count_from_m115_lines;
    REQUIRE(expected_probe_count_from_m115_lines({
        "FIRMWARE_NAME:Prusa-Firmware-Buddy ... MACHINE_TYPE:Prusa-XL ..."
    }) == 144);
}

TEST_CASE("expected_probe_count returns 81 for iX",
          "[bedmesh][probe_count]") {
    using Utils::expected_probe_count_from_m115_lines;
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:Prusa-iX"
    }) == 81);
}

TEST_CASE("expected_probe_count returns 16 for MINI",
          "[bedmesh][probe_count]") {
    using Utils::expected_probe_count_from_m115_lines;
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:Prusa-MINI"
    }) == 16);
}

TEST_CASE("expected_probe_count is case-insensitive",
          "[bedmesh][probe_count]") {
    using Utils::expected_probe_count_from_m115_lines;
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:prusa-core-one"
    }) == 49);
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:prusa-xl"
    }) == 144);
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:PRUSA-MINI"
    }) == 16);
}

TEST_CASE("expected_probe_count returns 0 for unknown or missing models",
          "[bedmesh][probe_count]") {
    using Utils::expected_probe_count_from_m115_lines;

    // No MACHINE_TYPE: line at all
    REQUIRE(expected_probe_count_from_m115_lines({
        "FIRMWARE_NAME:Some-Random-Firmware 1.0 ok"
    }) == 0);

    // Empty input
    REQUIRE(expected_probe_count_from_m115_lines({}) == 0);

    // Future / unrecognized model
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:Prusa-MK5"
    }) == 0);
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:Ender-3-Pro"
    }) == 0);
}

TEST_CASE("expected_probe_count scans multiple lines to find MACHINE_TYPE",
          "[bedmesh][probe_count]") {
    using Utils::expected_probe_count_from_m115_lines;
    REQUIRE(expected_probe_count_from_m115_lines({
        "echo: some unrelated banner",
        "Cap: MMU2:0",
        "MACHINE_TYPE:Prusa-XL",
        "ok"
    }) == 144);
}

TEST_CASE("expected_probe_count uses first match only",
          "[bedmesh][probe_count]") {
    using Utils::expected_probe_count_from_m115_lines;
    // If a line contains MACHINE_TYPE:, it stops scanning even if the match
    // is unknown — the first MACHINE_TYPE value wins.
    REQUIRE(expected_probe_count_from_m115_lines({
        "MACHINE_TYPE:Unknown-Printer",
        "MACHINE_TYPE:Prusa-XL"
    }) == 0);
}
