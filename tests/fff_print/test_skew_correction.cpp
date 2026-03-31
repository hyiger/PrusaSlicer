///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <memory>

#include "libslic3r/GCode.hpp"
#include "libslic3r/PrintConfig.hpp"

using namespace Slic3r;
using Catch::Approx;

// Helper to configure a GCodeGenerator with skew correction via PrintConfig.
// This tests the full config → apply → transform pipeline.
static std::unique_ptr<GCodeGenerator> make_skewed_generator(double skew_deg)
{
    PrintConfig config;
    config.set_deserialize_strict({
        { "skew_xy_correction", std::to_string(skew_deg) },
        { "nozzle_diameter",    "0.4" },
    });
    // Set a bed shape so the skew Y reference can be computed
    config.set_deserialize_strict({
        { "bed_shape", "0x0,250x0,250x210,0x210" },
    });

    auto gen = std::make_unique<GCodeGenerator>();
    gen->apply_print_config(config);
    // Initialize an extruder so point_to_gcode doesn't crash
    gen->writer().set_extruders({0});
    gen->writer().set_extruder(0);
    return gen;
}

TEST_CASE("Skew correction round-trip: point_to_gcode -> gcode_to_point", "[skew]")
{
    auto gen = make_skewed_generator(2.0); // 2 degree skew

    // Test several points across the bed
    std::vector<Point> test_points = {
        scaled(Vec2d(50.0, 50.0)),
        scaled(Vec2d(125.0, 105.0)),  // bed center
        scaled(Vec2d(0.0, 0.0)),
        scaled(Vec2d(200.0, 180.0)),
        scaled(Vec2d(10.0, 200.0)),   // high Y = large skew effect
    };

    for (const auto& pt : test_points) {
        Vec2d gcode_pt = gen->point_to_gcode(pt);
        Point recovered = gen->gcode_to_point(gcode_pt);

        INFO("Original: " << pt.x() << ", " << pt.y());
        INFO("G-code:   " << gcode_pt.x() << ", " << gcode_pt.y());
        INFO("Recovered: " << recovered.x() << ", " << recovered.y());

        // Should recover the original point within G-code resolution (~1 micron)
        CHECK(recovered.x() == Approx(pt.x()).margin(10)); // 10 nm tolerance
        CHECK(recovered.y() == Approx(pt.y()).margin(10));
    }
}

TEST_CASE("Skew correction: zero skew is identity", "[skew]")
{
    auto gen = make_skewed_generator(0.0);

    Point pt = scaled(Vec2d(100.0, 100.0));
    Vec2d gcode_pt = gen->point_to_gcode(pt);
    Point recovered = gen->gcode_to_point(gcode_pt);

    CHECK(recovered.x() == Approx(pt.x()).margin(10));
    CHECK(recovered.y() == Approx(pt.y()).margin(10));
}

TEST_CASE("Skew correction: Y coordinate unchanged by shear", "[skew]")
{
    auto gen = make_skewed_generator(3.0); // 3 degrees

    Point pt = scaled(Vec2d(100.0, 150.0));
    Vec2d gcode_pt = gen->point_to_gcode(pt);

    // Y should be unaffected by the shear (shear only modifies X)
    double expected_y = unscaled<double>(pt.y()); // origin is (0,0) by default
    CHECK(gcode_pt.y() == Approx(expected_y).margin(0.001));
}

TEST_CASE("Skew correction: X is shifted proportional to Y offset from bed center", "[skew]")
{
    double skew_deg = 2.0;
    auto gen = make_skewed_generator(skew_deg);
    double k = std::tan(skew_deg * M_PI / 180.0);
    double y_ref = (0.0 + 210.0) / 2.0; // bed center Y

    Point pt = scaled(Vec2d(100.0, 150.0));
    Vec2d gcode_pt = gen->point_to_gcode(pt);

    double orig_x = unscaled<double>(pt.x());
    double orig_y = unscaled<double>(pt.y());
    double expected_x = orig_x + (orig_y - y_ref) * k;

    CHECK(gcode_pt.x() == Approx(expected_x).margin(0.001));
}

TEST_CASE("Skew correction: negative angle inverts direction", "[skew]")
{
    auto gen_pos = make_skewed_generator(2.0);
    auto gen_neg = make_skewed_generator(-2.0);

    // Point above bed center — positive skew shifts X right, negative shifts left
    Point pt = scaled(Vec2d(100.0, 180.0)); // Y > bed center (105)
    Vec2d gcode_pos = gen_pos->point_to_gcode(pt);
    Vec2d gcode_neg = gen_neg->point_to_gcode(pt);
    double orig_x = unscaled<double>(pt.x());

    CHECK(gcode_pos.x() > orig_x);
    CHECK(gcode_neg.x() < orig_x);
}
