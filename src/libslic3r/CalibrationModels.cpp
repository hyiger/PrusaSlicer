///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationModels.hpp"
#include "TriangleMesh.hpp"
#include "MeshBoolean.hpp"
#include "ExPolygon.hpp"
#include "Tesselate.hpp"

#include <cmath>
#include <string>

namespace Slic3r {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Create a right-triangle cross-section prism (wedge).
static indexed_triangle_set its_make_wedge(double base_x, double height_z, double depth_y)
{
    auto bx = float(base_x), hz = float(height_z), dy = float(depth_y);

    return {
        {
            {0, 1, 2},
            {3, 5, 4},
            {0, 3, 4}, {0, 4, 1},
            {1, 4, 5}, {1, 5, 2},
            {0, 2, 5}, {0, 5, 3},
        },
        {
            Vec3f(0, 0, 0),
            Vec3f(bx, 0, 0),
            Vec3f(0, 0, hz),
            Vec3f(0, dy, 0),
            Vec3f(bx, dy, 0),
            Vec3f(0, dy, hz),
        }
    };
}

// ---------------------------------------------------------------------------
// Block-letter text engraving (no font loading required)
// ---------------------------------------------------------------------------

// 7x10 pixel font for digits 0-9 (normal orientation)
static const uint8_t DIGIT_FONT[10][10][7] = {
    // 0
    {{0,1,1,1,1,1,0},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{1,1,0,0,1,1,1},{1,1,0,1,0,1,1},{1,1,1,0,0,1,1},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{0,1,1,1,1,1,0}},
    // 1
    {{0,0,0,1,0,0,0},{0,0,1,1,0,0,0},{0,1,0,1,0,0,0},{0,0,0,1,0,0,0},{0,0,0,1,0,0,0},{0,0,0,1,0,0,0},{0,0,0,1,0,0,0},{0,0,0,1,0,0,0},{0,0,0,1,0,0,0},{0,1,1,1,1,1,0}},
    // 2
    {{0,1,1,1,1,1,0},{1,1,0,0,0,1,1},{0,0,0,0,0,1,1},{0,0,0,0,1,1,0},{0,0,0,1,1,0,0},{0,0,1,1,0,0,0},{0,1,1,0,0,0,0},{1,1,0,0,0,0,0},{1,1,0,0,0,1,1},{1,1,1,1,1,1,1}},
    // 3
    {{0,1,1,1,1,1,0},{1,1,0,0,0,1,1},{0,0,0,0,0,1,1},{0,0,0,0,0,1,1},{0,0,1,1,1,1,0},{0,0,0,0,0,1,1},{0,0,0,0,0,1,1},{0,0,0,0,0,1,1},{1,1,0,0,0,1,1},{0,1,1,1,1,1,0}},
    // 4
    {{0,0,0,0,1,1,0},{0,0,0,1,1,1,0},{0,0,1,0,1,1,0},{0,1,0,0,1,1,0},{1,0,0,0,1,1,0},{1,1,1,1,1,1,1},{0,0,0,0,1,1,0},{0,0,0,0,1,1,0},{0,0,0,0,1,1,0},{0,0,0,0,1,1,0}},
    // 5
    {{1,1,1,1,1,1,1},{1,1,0,0,0,0,0},{1,1,0,0,0,0,0},{1,1,1,1,1,1,0},{0,0,0,0,0,1,1},{0,0,0,0,0,1,1},{0,0,0,0,0,1,1},{0,0,0,0,0,1,1},{1,1,0,0,0,1,1},{0,1,1,1,1,1,0}},
    // 6
    {{0,1,1,1,1,1,0},{1,1,0,0,0,1,1},{1,1,0,0,0,0,0},{1,1,0,0,0,0,0},{1,1,1,1,1,1,0},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{0,1,1,1,1,1,0}},
    // 7
    {{1,1,1,1,1,1,1},{1,1,0,0,0,1,1},{0,0,0,0,0,1,1},{0,0,0,0,1,1,0},{0,0,0,1,1,0,0},{0,0,0,1,1,0,0},{0,0,0,1,1,0,0},{0,0,0,1,1,0,0},{0,0,0,1,1,0,0},{0,0,0,1,1,0,0}},
    // 8
    {{0,1,1,1,1,1,0},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{0,1,1,1,1,1,0},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{0,1,1,1,1,1,0}},
    // 9
    {{0,1,1,1,1,1,0},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{1,1,0,0,0,1,1},{0,1,1,1,1,1,1},{0,0,0,0,0,1,1},{0,0,0,0,0,1,1},{0,0,0,0,0,1,1},{1,1,0,0,0,1,1},{0,1,1,1,1,1,0}},
};

static constexpr int GLYPH_W  = 7;
static constexpr int GLYPH_H  = 10;
static constexpr int GLYPH_SP = 2;

static indexed_triangle_set make_block_text(const std::string& text, double height_mm, double depth_mm)
{
    std::vector<int> digits;
    for (char c : text)
        if (c >= '0' && c <= '9')
            digits.push_back(c - '0');
    if (digits.empty())
        return {};

    double pixel_size = height_mm / GLYPH_H;
    int total_w = int(digits.size()) * GLYPH_W + (int(digits.size()) - 1) * GLYPH_SP;
    double total_width  = total_w * pixel_size;
    double total_height = GLYPH_H * pixel_size;

    indexed_triangle_set result;

    for (size_t di = 0; di < digits.size(); ++di) {
        int digit = digits[di];
        int x_offset_px = int(di) * (GLYPH_W + GLYPH_SP);

        for (int row = 0; row < GLYPH_H; ++row) {
            for (int col = 0; col < GLYPH_W; ++col) {
                if (!DIGIT_FONT[digit][row][col])
                    continue;

                auto block = its_make_cube(pixel_size, depth_mm, pixel_size);

                double x = (x_offset_px + col) * pixel_size - total_width / 2.0;
                double z = (GLYPH_H - 1 - row) * pixel_size - total_height / 2.0;

                its_translate(block, Vec3f(float(x), 0.f, float(z)));
                its_merge(result, block);
            }
        }
    }

    // Mirror in X so text reads correctly after the 180° Z rotation in make_temp_tower
    for (auto& v : result.vertices)
        v.x() = -v.x();
    for (auto& f : result.indices)
        std::swap(f[0], f[1]);

    return result;
}

// ---------------------------------------------------------------------------
// Flow Specimen (serpentine / E-shape) — built as extruded 2D polygon
// ---------------------------------------------------------------------------

// Scaling factor: ExPolygon uses integer coordinates in nanometers
static constexpr double FLOW_SCALE = 1e6;  // mm to nanometers

// Append arc points to a polygon. cx,cy in mm, r in mm.
// a_start/a_end in radians. Points are appended in order.
static void append_arc(Polygon& poly, double cx, double cy, double r,
                       double a_start, double a_end, int n = 32)
{
    for (int i = 0; i <= n; ++i) {
        double a = a_start + (a_end - a_start) * i / n;
        double x = cx + r * std::cos(a);
        double y = cy + r * std::sin(a);
        poly.points.push_back(Point(coord_t(x * FLOW_SCALE), coord_t(y * FLOW_SCALE)));
    }
}

// Extrude an ExPolygon to a 3D mesh from z=0 to z=height.
// Uses simple triangulation: triangle fan for top/bottom, quads for sides.
static indexed_triangle_set extrude_expolygon(const ExPolygon& expoly, double height)
{
    indexed_triangle_set result;

    // Collect all contours: outer + holes
    std::vector<const Polygon*> contours;
    contours.push_back(&expoly.contour);
    for (const auto& h : expoly.holes)
        contours.push_back(&h);

    // For each contour, create side walls
    for (const Polygon* poly : contours) {
        int n = int(poly->points.size());
        if (n < 3) continue;

        int base = int(result.vertices.size());

        // Add bottom and top vertices
        for (int i = 0; i < n; ++i) {
            float x = float(poly->points[i].x()) / float(FLOW_SCALE);
            float y = float(poly->points[i].y()) / float(FLOW_SCALE);
            result.vertices.push_back(Vec3f(x, y, 0.f));
        }
        for (int i = 0; i < n; ++i) {
            float x = float(poly->points[i].x()) / float(FLOW_SCALE);
            float y = float(poly->points[i].y()) / float(FLOW_SCALE);
            result.vertices.push_back(Vec3f(x, y, float(height)));
        }

        // Side quads
        for (int i = 0; i < n; ++i) {
            int j = (i + 1) % n;
            int b0 = base + i, b1 = base + j;
            int t0 = base + n + i, t1 = base + n + j;
            result.indices.push_back({b0, b1, t1});
            result.indices.push_back({b0, t1, t0});
        }
    }

    // Top/bottom faces: use PrusaSlicer's tessellation for proper triangulation.
    // Ensure correct winding: contour CCW, holes CW.
    {
        ExPolygon ep = expoly;

        // Ensure correct orientation
        ep.contour.make_counter_clockwise();
        for (auto& hole : ep.holes)
            hole.make_clockwise();

        // triangulate_expolygon_3d returns coordinates in mm (unscaled)
        auto bot_tris = triangulate_expolygon_3d(ep, 0.0, true);
        int base = int(result.vertices.size());
        for (size_t i = 0; i < bot_tris.size(); ++i) {
            result.vertices.push_back(Vec3f(float(bot_tris[i].x()), float(bot_tris[i].y()), 0.f));
        }
        for (size_t i = 0; i + 2 < bot_tris.size(); i += 3) {
            result.indices.push_back({base + int(i), base + int(i + 1), base + int(i + 2)});
        }

        // Top face
        auto top_tris = triangulate_expolygon_3d(ep, 0.0, false);
        base = int(result.vertices.size());
        for (size_t i = 0; i < top_tris.size(); ++i) {
            result.vertices.push_back(Vec3f(float(top_tris[i].x()), float(top_tris[i].y()), float(height)));
        }
        for (size_t i = 0; i + 2 < top_tris.size(); i += 3) {
            result.indices.push_back({base + int(i), base + int(i + 1), base + int(i + 2)});
        }
    }

    return result;
}

indexed_triangle_set make_flow_specimen(
    int    num_levels,
    double level_height,
    double width,
    double arm_thickness,
    double gap_width,
    int    num_arms)
{
    double depth  = num_arms * arm_thickness + (num_arms - 1) * gap_width;
    double height = num_levels * level_height;

    double inner_r = gap_width / 2.0;
    double outer_r = arm_thickness / 2.0;
    double spine_r = arm_thickness - 0.5;

    double left_x  = -width / 2.0;
    double right_x =  width / 2.0;
    double spine_x = left_x + arm_thickness;
    double bot_y   = -depth / 2.0;
    double top_y   =  depth / 2.0;

    // Build as a SINGLE polygon (no holes) by tracing the entire serpentine
    // boundary. The outer contour traces the top edge of the ribbon going
    // right, then the bottom edge going left. Between arms, the contour
    // dips into the slot with rounded ends on both sides.
    //
    // For 3 arms the path is (CCW):
    //   Bottom-left spine arc → bottom of arm 0 → arm 0 tip arc →
    //   top of arm 0 → into slot 0 (right semicircle) → across slot top →
    //   slot 0 left semicircle → across slot bottom → slot 0 right end →
    //   bottom of arm 1 tip → arm 1 tip arc → top of arm 1 →
    //   into slot 1 ... → arm 2 tip arc → top of arm 2 →
    //   top-left spine arc → left edge closes.
    //
    // This avoids all hole/contour shared-edge issues.

    double slot_left_cx = spine_x + inner_r;  // left semicircle center X
    double slot_right_x = right_x - outer_r;  // right end of slot (= arm tip arc center X)

    Polygon outer;

    // Bottom-left spine corner
    append_arc(outer, left_x + spine_r, bot_y + spine_r, spine_r, M_PI, 3.0 * M_PI / 2.0);

    for (int i = 0; i < num_arms; ++i) {
        double yb = bot_y + i * (arm_thickness + gap_width);
        double yt = yb + arm_thickness;
        double arm_cy = (yb + yt) / 2.0;

        // Bottom edge of arm → arm tip arc
        outer.points.push_back(Point(coord_t(slot_right_x * FLOW_SCALE), coord_t(yb * FLOW_SCALE)));
        append_arc(outer, slot_right_x, arm_cy, outer_r, -M_PI / 2.0, M_PI / 2.0);

        if (i < num_arms - 1) {
            // Trace INTO the slot gap:
            // We're at (slot_right_x, yt) after the arm tip arc.
            // Go left along top of slot to spine, around left semicircle,
            // right along bottom of slot back to slot_right_x.
            double slot_cy = yt + gap_width / 2.0;

            // Bottom of slot (= top of arm i): right to left
            double next_yb = yt + gap_width;
            outer.points.push_back(Point(coord_t(slot_left_cx * FLOW_SCALE), coord_t(yt * FLOW_SCALE)));

            // Left semicircle of slot: from -90° to +90° the LONG way
            // through 180° (curving left into the spine)
            append_arc(outer, slot_left_cx, slot_cy, inner_r, -M_PI / 2.0, -3.0 * M_PI / 2.0);

            // Top of slot (= bottom of arm i+1): left to right
            outer.points.push_back(Point(coord_t(slot_right_x * FLOW_SCALE), coord_t(next_yb * FLOW_SCALE)));
            // Next iteration picks up with the next arm tip arc
        }
    }

    // After last arm tip arc, we're at (slot_right_x, top_y).
    // Top edge back to top-left spine corner
    // Top-left spine corner
    append_arc(outer, left_x + spine_r, top_y - spine_r, spine_r, M_PI / 2.0, M_PI);

    // Left edge closes implicitly back to start

    ExPolygon expoly;
    expoly.contour = outer;
    // No holes!

    auto shape = extrude_expolygon(expoly, height);
    return shape;
}

// ---------------------------------------------------------------------------
// Temperature Tower
// ---------------------------------------------------------------------------

static constexpr double BASE_LENGTH     = 89.3;
static constexpr double BASE_WIDTH      = 20.0;
static constexpr double BASE_HEIGHT     = 1.0;

static constexpr double TIER_LENGTH     = 79.0;
static constexpr double TIER_WIDTH      = 10.0;
static constexpr double TIER_HEIGHT     = 10.0;

static constexpr double OVERHANG_45_X   = 10.0;
static constexpr double OVERHANG_35_X   = 14.281;

static constexpr double CUTOUT_LENGTH   = 30.0;
static constexpr double CUTOUT_HEIGHT   = 9.0;
static constexpr double CUTOUT_OFFSET   = 15.0;

static constexpr double CONE_HEIGHT     = 5.0;
static constexpr double SM_CONE_DIAM    = 3.0;
static constexpr double SM_CONE_OFFSET  = 5.0;
static constexpr double LG_CONE_DIAM    = 5.0;
static constexpr double LG_CONE_OFFSET  = 25.0;

static constexpr double HOLE_DIAM       = 3.0;
static constexpr double HOLE_45_OFFSET  = 3.671;
static constexpr double HOLE_35_OFFSET  = 75.0;
static constexpr double HORIZ_HOLE_LEN  = 5.0;

static constexpr double PROTRUSION_LENGTH = 16.0;
static constexpr double PROTRUSION_HEIGHT = 0.7;
static constexpr double PROTRUSION_DEPTH  = 0.5;
static constexpr double TEST_CUTOUT_H_OFFSET = 47.0;
static constexpr double TEST_CUTOUT_V_OFFSET = 0.3;

static constexpr double TEMP_LABEL_SIZE     = 6.0;
static constexpr double TEMP_LABEL_DEPTH    = 0.6;
static constexpr double TEMP_LABEL_V_OFFSET = 5.0;
static constexpr double TEMP_LABEL_H_OFFSET = 25.0;

static indexed_triangle_set make_tier(int temperature)
{
    auto tier = its_make_cube(TIER_LENGTH, TIER_WIDTH, TIER_HEIGHT);

    // 45-degree overhang cut (left side)
    {
        auto wedge = its_make_wedge(OVERHANG_45_X, TIER_HEIGHT, TIER_WIDTH);
        MeshBoolean::cgal::minus(tier, wedge);
    }

    // 35-degree overhang cut (right side)
    {
        auto wedge = its_make_wedge(OVERHANG_35_X, TIER_HEIGHT, TIER_WIDTH);
        for (auto& v : wedge.vertices)
            v.x() = -v.x();
        its_translate(wedge, Vec3f(float(TIER_LENGTH), 0.f, 0.f));
        for (auto& f : wedge.indices)
            std::swap(f[0], f[1]);
        MeshBoolean::cgal::minus(tier, wedge);
    }

    // Central rectangular cutout
    {
        auto cutout = its_make_cube(CUTOUT_LENGTH, TIER_WIDTH + 2.0, CUTOUT_HEIGHT);
        its_translate(cutout, Vec3f(float(CUTOUT_OFFSET), -1.f, 0.f));
        MeshBoolean::cgal::minus(tier, cutout);
    }

    // Vertical hole near 45-degree overhang
    {
        auto hole = its_make_cylinder(HOLE_DIAM / 2.0, TIER_HEIGHT + 2.0);
        its_translate(hole, Vec3f(float(HOLE_45_OFFSET), float(TIER_WIDTH / 2.0), -1.f));
        MeshBoolean::cgal::minus(tier, hole);
    }

    // Vertical hole near 35-degree overhang
    {
        auto hole = its_make_cylinder(HOLE_DIAM / 2.0, TIER_HEIGHT + 2.0);
        its_translate(hole, Vec3f(float(HOLE_35_OFFSET), float(TIER_WIDTH / 2.0), -1.f));
        MeshBoolean::cgal::minus(tier, hole);
    }

    // Horizontal hole through the bridge area
    {
        auto hole = its_make_cylinder(HOLE_DIAM / 2.0, HORIZ_HOLE_LEN);
        Transform3d rot = Transform3d::Identity();
        rot.rotate(Eigen::AngleAxisd(M_PI / 2.0, Vec3d::UnitX()));
        its_transform(hole, rot);
        its_translate(hole, Vec3f(
            float(CUTOUT_OFFSET + CUTOUT_LENGTH),
            float(TIER_WIDTH),
            float(TIER_HEIGHT / 2.0)));
        MeshBoolean::cgal::minus(tier, hole);
    }

    // Temperature label raised on back face
    if (temperature > 0) {
        auto text_mesh = make_block_text(std::to_string(temperature), TEMP_LABEL_SIZE, TEMP_LABEL_DEPTH);
        if (!text_mesh.empty()) {
            its_translate(text_mesh, Vec3f(
                float(TIER_LENGTH - TEMP_LABEL_H_OFFSET),
                float(TIER_WIDTH),
                float(TEMP_LABEL_V_OFFSET)));
            its_merge(tier, text_mesh);
        }
    }

    // Small cone inside cutout
    {
        auto cone = its_make_cone(SM_CONE_DIAM / 2.0, CONE_HEIGHT);
        its_translate(cone, Vec3f(
            float(CUTOUT_OFFSET + SM_CONE_OFFSET),
            float(TIER_WIDTH / 2.0),
            0.f));
        its_merge(tier, cone);
    }

    // Large cone inside cutout
    {
        auto cone = its_make_cone(LG_CONE_DIAM / 2.0, CONE_HEIGHT);
        its_translate(cone, Vec3f(
            float(CUTOUT_OFFSET + LG_CONE_OFFSET),
            float(TIER_WIDTH / 2.0),
            0.f));
        its_merge(tier, cone);
    }

    // Test protrusion bar on front face
    {
        auto bar = its_make_cube(PROTRUSION_LENGTH, PROTRUSION_DEPTH, PROTRUSION_HEIGHT);
        its_translate(bar, Vec3f(
            float(TEST_CUTOUT_H_OFFSET),
            float(-PROTRUSION_DEPTH),
            float(TEST_CUTOUT_V_OFFSET)));
        its_merge(tier, bar);
    }

    return tier;
}

indexed_triangle_set make_temp_tower(int num_tiers, int start_temp, int temp_step)
{
    auto tower = its_make_cube(BASE_LENGTH, BASE_WIDTH, BASE_HEIGHT);
    its_translate(tower, Vec3f(float(-BASE_LENGTH / 2.0), float(-BASE_WIDTH / 2.0), 0.f));

    for (int i = 0; i < num_tiers; ++i) {
        int temperature = start_temp - i * temp_step;
        auto tier = make_tier(temperature);

        its_translate(tier, Vec3f(
            float(-TIER_LENGTH / 2.0),
            float(-TIER_WIDTH / 2.0),
            float(BASE_HEIGHT + i * TIER_HEIGHT)));

        try {
            MeshBoolean::cgal::plus(tower, tier);
        } catch (...) {
            its_merge(tower, tier);
        }
    }

    Transform3d rot = Transform3d::Identity();
    rot.rotate(Eigen::AngleAxisd(M_PI, Vec3d::UnitZ()));
    its_transform(tower, rot);

    return tower;
}

} // namespace Slic3r
