///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "libslic3r/CalibrationModels.hpp"
#include "libslic3r/TriangleMesh.hpp"

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
