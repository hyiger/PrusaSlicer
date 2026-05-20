///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "libslic3r/CalibrationModels.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/GCode/CalibrationRetractionPostProcessor.hpp"

#include <boost/filesystem.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r;
using Catch::Approx;

// Helper: check that an indexed_triangle_set is non-empty and has
// consistent vertex/face counts.
static void check_mesh_valid(const indexed_triangle_set& its,
                             const char* label)
{
    INFO(label);
    REQUIRE(!its.vertices.empty());
    REQUIRE(!its.indices.empty());
    // Every face index must be in range
    for (const auto& f : its.indices) {
        CHECK(f[0] >= 0);
        CHECK(f[1] >= 0);
        CHECK(f[2] >= 0);
        CHECK(f[0] < (int)its.vertices.size());
        CHECK(f[1] < (int)its.vertices.size());
        CHECK(f[2] < (int)its.vertices.size());
    }
}

// Helper: bounding box of an indexed_triangle_set
static BoundingBoxf3 its_bbox(const indexed_triangle_set& its)
{
    BoundingBoxf3 bb;
    for (const auto& v : its.vertices)
        bb.merge(v.cast<double>());
    return bb;
}

// -----------------------------------------------------------------------
// Temperature Tower
// -----------------------------------------------------------------------

TEST_CASE("make_temp_tower basic mesh validity", "[calibration]")
{
    auto its = make_temp_tower(5, 250, 5);
    check_mesh_valid(its, "temp_tower 5 tiers");
}

TEST_CASE("make_temp_tower height matches tier count", "[calibration]")
{
    int num_tiers = 4;
    auto its = make_temp_tower(num_tiers, 230, 10);
    auto bb = its_bbox(its);

    double expected_height = TEMP_TOWER_BASE_HEIGHT + num_tiers * TEMP_TOWER_TIER_HEIGHT;
    CHECK(bb.max.z() == Approx(expected_height).margin(0.5));
}

TEST_CASE("make_temp_tower single tier", "[calibration]")
{
    auto its = make_temp_tower(1, 200, 5);
    check_mesh_valid(its, "temp_tower 1 tier");
}

// -----------------------------------------------------------------------
// Flow Specimen
// -----------------------------------------------------------------------

TEST_CASE("make_flow_specimen basic validity", "[calibration]")
{
    auto its = make_flow_specimen(5);
    check_mesh_valid(its, "flow_specimen defaults");
}

TEST_CASE("make_flow_specimen edge cases return empty", "[calibration]")
{
    CHECK(make_flow_specimen(0).vertices.empty());
    CHECK(make_flow_specimen(-1).vertices.empty());
    CHECK(make_flow_specimen(5, 1.0, 170.0, 20.0, 20.0, 0).vertices.empty());
    CHECK(make_flow_specimen(5, 1.0, 170.0, 0.0).vertices.empty());
    CHECK(make_flow_specimen(5, 0.0).vertices.empty());
}

// -----------------------------------------------------------------------
// PA Pattern
// -----------------------------------------------------------------------

TEST_CASE("make_pa_pattern basic validity", "[calibration]")
{
    auto its = make_pa_pattern(20, 0.2, 90.0, 40.0, 1.6);
    check_mesh_valid(its, "pa_pattern defaults");
}

TEST_CASE("make_pa_pattern edge cases return empty", "[calibration]")
{
    // corner_angle at extremes
    CHECK(make_pa_pattern(20, 0.2, 0.0).vertices.empty());
    CHECK(make_pa_pattern(20, 0.2, 180.0).vertices.empty());
    CHECK(make_pa_pattern(20, 0.2, -10.0).vertices.empty());
    // zero/negative layers
    CHECK(make_pa_pattern(0).vertices.empty());
    CHECK(make_pa_pattern(-1).vertices.empty());
    // zero layer height
    CHECK(make_pa_pattern(20, 0.0).vertices.empty());
}

TEST_CASE("make_pa_pattern height matches layers", "[calibration]")
{
    int layers = 10;
    double lh = 0.2;
    auto its = make_pa_pattern(layers, lh);
    auto bb = its_bbox(its);
    CHECK(bb.max.z() == Approx(layers * lh).margin(0.01));
}

// -----------------------------------------------------------------------
// Retraction Towers
// -----------------------------------------------------------------------

TEST_CASE("make_retraction_towers basic validity", "[calibration]")
{
    auto its = make_retraction_towers(50.0, 10.0, 50.0);
    check_mesh_valid(its, "retraction_towers defaults");
}

TEST_CASE("make_retraction_towers edge cases return empty", "[calibration]")
{
    // height <= base height (1.0)
    CHECK(make_retraction_towers(1.0).vertices.empty());
    CHECK(make_retraction_towers(0.5).vertices.empty());
    // zero diameter
    CHECK(make_retraction_towers(50.0, 0.0).vertices.empty());
    // zero spacing
    CHECK(make_retraction_towers(50.0, 10.0, 0.0).vertices.empty());
}

// -----------------------------------------------------------------------
// Block Text
// -----------------------------------------------------------------------

TEST_CASE("make_block_text digits produce mesh", "[calibration]")
{
    auto its = make_block_text("123", 5.0, 1.0);
    check_mesh_valid(its, "block_text digits");
}

TEST_CASE("make_block_text special chars", "[calibration]")
{
    // percent, minus, plus, period are supported
    auto its = make_block_text("-5.0%", 5.0, 1.0);
    check_mesh_valid(its, "block_text special");
}

TEST_CASE("make_block_text unsupported chars return empty", "[calibration]")
{
    CHECK(make_block_text("ABC", 5.0, 1.0).vertices.empty());
    CHECK(make_block_text("", 5.0, 1.0).vertices.empty());
}

TEST_CASE("make_block_text height scales correctly", "[calibration]")
{
    auto small = make_block_text("1", 2.0, 1.0, false);
    auto large = make_block_text("1", 8.0, 1.0, false);

    auto bb_small = its_bbox(small);
    auto bb_large = its_bbox(large);

    // The larger text should be roughly 4x taller in Z
    double ratio = (bb_large.max.z() - bb_large.min.z()) /
                   (bb_small.max.z() - bb_small.min.z());
    CHECK(ratio == Approx(4.0).margin(0.5));
}

// -----------------------------------------------------------------------
// Fan Tower
// -----------------------------------------------------------------------

TEST_CASE("make_fan_tower basic validity", "[calibration]")
{
    auto its = make_fan_tower(11);
    check_mesh_valid(its, "fan_tower 11 levels");
}

TEST_CASE("make_fan_tower height matches levels", "[calibration]")
{
    int levels = 5;
    auto its = make_fan_tower(levels);
    auto bb = its_bbox(its);

    double expected_height = 1.0 + levels * FAN_TOWER_LEVEL_HEIGHT; // FAN_BASE_H=1.0
    CHECK(bb.max.z() == Approx(expected_height).margin(1.0));
}

TEST_CASE("make_fan_tower single level", "[calibration]")
{
    auto its = make_fan_tower(1);
    check_mesh_valid(its, "fan_tower 1 level");
}

// -----------------------------------------------------------------------
// Shrinkage Gauge
// -----------------------------------------------------------------------

TEST_CASE("make_shrinkage_gauge basic validity", "[calibration]")
{
    auto its = make_shrinkage_gauge(100.0);
    check_mesh_valid(its, "shrinkage_gauge 100mm");
}

TEST_CASE("make_shrinkage_gauge arm length", "[calibration]")
{
    double length = 75.0;
    auto its = make_shrinkage_gauge(length);
    auto bb = its_bbox(its);

    // Arms extend from origin; total span = length (plus labels protrude slightly)
    double span_x = bb.max.x() - bb.min.x();
    double span_y = bb.max.y() - bb.min.y();
    double span_z = bb.max.z() - bb.min.z();
    CHECK(span_x >= length - 5.0);
    CHECK(span_y >= length - 5.0);
    CHECK(span_z >= length - 5.0);
}

// ---------------------------------------------------------------------------
// Retraction Calibration Post-Processor
// ---------------------------------------------------------------------------

static std::string slurp(const std::string& path)
{
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static std::string write_tmp_gcode(const std::string& content)
{
    auto p = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("retcal-%%%%-%%%%.gcode");
    std::ofstream f(p.string(), std::ios::binary);
    f << content;
    f.close();
    return p.string();
}

TEST_CASE("calibration retraction: URL round-trip", "[calibration]")
{
    std::vector<std::pair<double, double>> levels = {
        {2.0, 0.0}, {3.0, 0.2}, {4.0, 0.4}};
    auto url = make_calibration_retraction_url(0.7, levels);
    CHECK(is_calibration_retraction_url(url));
    CHECK(url.find("base=0.7000") != std::string::npos);
    CHECK(url.find("2.0000:0.0000") != std::string::npos);
    CHECK(url.find("4.0000:0.4000") != std::string::npos);

    // A non-builtin path should not be misidentified as a builtin URL
    CHECK_FALSE(is_calibration_retraction_url("/usr/local/bin/my_script.py"));
    CHECK_FALSE(is_calibration_retraction_url("::builtin::other_calibration?foo=bar"));
}

TEST_CASE("calibration retraction: rewrites retract/recovery by Z band", "[calibration]")
{
    const std::string input =
        ";Z:0.2\n"
        "G1 X10 Y10 E0.5 F1500\n"
        "G1 E-1.6 F2700\n"
        "G1 X20 Y20 F21000\n"
        "G1 E1.6 F1500\n"
        ";Z:1.2\n"
        "G1 E-1.6 F2700\n"
        "G1 X30 Y30 F21000\n"
        "G1 E1.6 F1500\n"
        ";Z:5.2\n"
        "G1 E-1.6 F2700\n"
        "G1 X40 Y40 F21000\n"
        "G1 E1.6 F1500\n";

    std::vector<std::pair<double, double>> levels = {
        {2.0, 0.0}, {3.0, 0.2}, {4.0, 0.4}, {5.0, 0.6},
        {6.0, 0.8}, {7.0, 1.0}, {8.0, 1.2}, {9.0, 1.4}, {10.0, 1.6}};
    auto url  = make_calibration_retraction_url(0.7, levels);
    auto path = write_tmp_gcode(input);

    REQUIRE(run_calibration_retraction_post_processor(url, path));
    auto out = slurp(path);
    boost::filesystem::remove(path);

    // Base layer (Z=0.2 < 1.0) uses BASE_RETRACT = 0.7
    CHECK(out.find("G1 E-0.7000") != std::string::npos);
    CHECK(out.find("; r z=0.20") != std::string::npos);

    // Z=1.2 is in [1.0, 2.0) → level 0 = 0.0
    CHECK(out.find("; r z=1.20") != std::string::npos);

    // Z=5.2 is in [5.0, 6.0) → level 4 = 0.8
    CHECK(out.find("G1 E-0.8000") != std::string::npos);
    CHECK(out.find("; r z=5.20") != std::string::npos);
    CHECK(out.find("; R z=5.20") != std::string::npos); // recovery at Z=5.2

    // Sanity: extrusion lines and Z comments are preserved verbatim
    CHECK(out.find(";Z:0.2") != std::string::npos);
    CHECK(out.find("G1 X10 Y10 E0.5 F1500") != std::string::npos);
    CHECK(out.find("G1 X40 Y40 F21000") != std::string::npos);
}

TEST_CASE("calibration retraction: leaves non-retract E lines untouched", "[calibration]")
{
    // PrusaSlicer's combined-axis G1 moves (X/Y + E) must NOT be rewritten —
    // those are extrusion paths during printing, not retract/recovery moves.
    const std::string input =
        ";Z:1.2\n"
        "G1 X10 Y10 E0.5 F1500\n"
        "G1 X20 Y20 E-0.1 F1500\n"  // (synthetic) negative-E extrusion line
        "G1 E-1.6 F2700\n"          // this IS a retract; rewrite to level 0 (= 0.0)
        "G1 X30 Y30 F21000\n";

    auto url  = make_calibration_retraction_url(0.7, {{2.0, 0.0}});
    auto path = write_tmp_gcode(input);
    REQUIRE(run_calibration_retraction_post_processor(url, path));
    auto out = slurp(path);
    boost::filesystem::remove(path);

    // Lines with X/Y must be byte-identical
    CHECK(out.find("G1 X10 Y10 E0.5 F1500\n") != std::string::npos);
    CHECK(out.find("G1 X20 Y20 E-0.1 F1500\n") != std::string::npos);

    // The retract-only line is rewritten
    CHECK(out.find("G1 E-0.0000") != std::string::npos);
}

TEST_CASE("calibration retraction: Z tracking via G1 Z moves", "[calibration]")
{
    // Track Z even when only a `G1 Z` move appears (no `;Z:` comment).
    const std::string input =
        "G1 Z1.2 F720\n"
        "G1 E-1.6 F2700\n"
        "G1 Z5.4 F720\n"
        "G1 E-1.6 F2700\n";

    auto url  = make_calibration_retraction_url(0.7,
        {{2.0, 0.0}, {3.0, 0.2}, {4.0, 0.4}, {5.0, 0.6}, {6.0, 0.8}});
    auto path = write_tmp_gcode(input);
    REQUIRE(run_calibration_retraction_post_processor(url, path));
    auto out = slurp(path);
    boost::filesystem::remove(path);

    // First retract is at Z=1.2 → level 0 = 0.0
    CHECK(out.find("G1 E-0.0000") != std::string::npos);
    // Second retract is at Z=5.4 → level 4 = 0.8
    CHECK(out.find("G1 E-0.8000") != std::string::npos);
}

TEST_CASE("calibration retraction: recovery matches its retract across band boundary", "[calibration]")
{
    // The previous level's retract followed by the next level's recovery would
    // drop a small plastic blob at the seam on every band boundary if the two
    // values were looked up independently from current Z. Verify the recovery
    // mirrors its matching retract, regardless of the Z change in between.
    const std::string input =
        ";Z:1.8\n"
        "G1 E-1.7 F2700\n"          // retract at end of band-0 layer
        "G1 X20 Y20 F21000\n"       // travel
        "G1 Z2.0 F720\n"            // layer change INTO band 1
        "G1 E1.7 F1500\n";          // recovery — must use band-0 value, NOT band-1

    auto url  = make_calibration_retraction_url(0.7, {{2.0, 0.0}, {3.0, 0.1}});
    auto path = write_tmp_gcode(input);
    REQUIRE(run_calibration_retraction_post_processor(url, path));
    auto out = slurp(path);
    boost::filesystem::remove(path);

    // Retract at z=1.8 uses band 0 = 0.0
    CHECK(out.find("G1 E-0.0000 F2700 ; r z=1.80") != std::string::npos);
    // Recovery at z=2.0 must match: 0.0 (NOT 0.1, which would be the band-1 lookup)
    CHECK(out.find("G1 E0.0000 F1500 ; R z=2.00") != std::string::npos);
    // No 0.1 anywhere — that would be the bug signature
    CHECK(out.find("G1 E0.1000") == std::string::npos);
    CHECK(out.find("G1 E-0.1000") == std::string::npos);
}

TEST_CASE("calibration retraction: refuses binary G-code input", "[calibration]")
{
    // A file starting with "GCDE" (bgcode magic) is binary G-code. The rewriter
    // must refuse loudly instead of silently no-op'ing on a binary blob.
    auto path = write_tmp_gcode(std::string("GCDE\x01\x00\x00\x00\x01\x00", 10));
    auto url  = make_calibration_retraction_url(0.7, {{2.0, 0.0}});
    CHECK_THROWS_WITH(run_calibration_retraction_post_processor(url, path),
                      Catch::Matchers::ContainsSubstring("binary G-code"));
    boost::filesystem::remove(path);
}

TEST_CASE("calibration retraction: malformed URL throws", "[calibration]")
{
    auto path = write_tmp_gcode("G1 E-1.6 F2700\n");
    CHECK_THROWS(run_calibration_retraction_post_processor(
        "::builtin::retraction_calibration?base=0.7", path));  // missing levels
    CHECK_THROWS(run_calibration_retraction_post_processor(
        "::builtin::retraction_calibration?levels=2.0:0.0", path));  // missing base
    boost::filesystem::remove(path);
}
