///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationModels.hpp"
#include "TriangleMesh.hpp"
#include "MeshBoolean.hpp"
#include "ExPolygon.hpp"
#include "Tesselate.hpp"
#include "libslic3r.h"  // SCALING_FACTOR, scale_(), unscale<>()

#include <boost/log/trivial.hpp>

#include <cmath>
#include <string>
#include <unordered_map>

namespace Slic3r {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Create a right-triangle cross-section prism (wedge) for overhang cuts.
/// The right-angle corner sits at the origin; the hypotenuse runs from
/// (base_x, 0, 0) to (0, 0, height_z).  Extruded depth_y in +Y.
static indexed_triangle_set its_make_wedge(double base_x, double height_z, double depth_y)
{
    auto bx = float(base_x), hz = float(height_z), dy = float(depth_y);

    return {
        // 8 triangles: 2 end-caps + 3 quads (each split into 2 tris)
        {
            {0, 1, 2},            // front end-cap  (y = 0)
            {3, 5, 4},            // back  end-cap  (y = dy)
            {0, 3, 4}, {0, 4, 1}, // bottom quad    (z = 0)
            {1, 4, 5}, {1, 5, 2}, // hypotenuse quad
            {0, 2, 5}, {0, 5, 3}, // left side quad (x = 0)
        },
        {
            Vec3f(0, 0, 0),      // 0: front origin
            Vec3f(bx, 0, 0),     // 1: front base
            Vec3f(0, 0, hz),     // 2: front top
            Vec3f(0, dy, 0),     // 3: back origin
            Vec3f(bx, dy, 0),    // 4: back base
            Vec3f(0, dy, hz),    // 5: back top
        }
    };
}

// ---------------------------------------------------------------------------
// Block-letter text engraving (no font loading required)
// ---------------------------------------------------------------------------

/// Bitmap font for digits 0-9.  Each glyph is a 7-wide × 10-tall grid of
/// 0/1 values (row-major, top to bottom).  Used by make_block_text() to
/// create raised pixel-block labels on the temperature tower tiers.
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

static constexpr int GLYPH_W  = 7;   // pixels wide per digit
static constexpr int GLYPH_H  = 10;  // pixels tall per digit
static constexpr int GLYPH_SP = 2;   // pixel spacing between digits

/// Build a block-letter mesh for a numeric string.
/// Each lit pixel becomes a small cube; the whole label is centred at origin
/// in XZ, extruded depth_mm in +Y.  height_mm controls overall glyph height.
/// The mesh is pre-mirrored in X so it reads correctly after the 180° Z
/// rotation applied in make_temp_tower().
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

// PrusaSlicer's Polygon uses integer coord_t (nanometer scale).
// Use the standard scale_() macro to convert mm → coord_t.

/// Append n+1 arc points to a polygon.
/// Centre (cx, cy) and radius r are in mm; angles in radians.
/// The arc interpolates linearly from a_start to a_end (which may cross
/// ±2π for "long way around" arcs).
static void append_arc(Polygon& poly, double cx, double cy, double r,
                       double a_start, double a_end, int n = 32)
{
    for (int i = 0; i <= n; ++i) {
        double a = a_start + (a_end - a_start) * i / n;
        double x = cx + r * std::cos(a);
        double y = cy + r * std::sin(a);
        poly.points.push_back(Point(coord_t(scale_(x)), coord_t(scale_(y))));
    }
}

/// Extrude an ExPolygon into a closed 3D mesh from z=0 to z=height.
/// Side walls are built as quad strips from each contour edge.
/// Top and bottom caps use PrusaSlicer's GLU tessellator to correctly
/// triangulate concave polygons with holes.
///
/// All vertices are computed once via a canonical conversion (coord_t → float)
/// and shared between side walls and caps to avoid float-rounding mismatches
/// that cause near-degenerate facet warnings during mesh repair.
static indexed_triangle_set extrude_expolygon(const ExPolygon& expoly, double height)
{
    indexed_triangle_set result;

    // --- Phase 1: Create shared vertices and build a lookup table. ---
    // For each polygon point we create exactly two vertices (z=0 and z=height)
    // and record their indices so caps can reference them.
    //
    // vertex_base[point_index] gives the index into result.vertices for the
    // z=0 vertex; vertex_base[point_index] + total_pts gives the z=height one.

    // Flatten all contour points into a single list (outer contour first,
    // then holes) and record where each contour starts.
    std::vector<Vec3f>  flat_pts;       // canonical XY for each point
    std::vector<int>    contour_starts; // start index in flat_pts per contour
    std::vector<int>    contour_sizes;  // number of points per contour

    auto add_contour = [&](const Polygon& poly) {
        contour_starts.push_back(int(flat_pts.size()));
        contour_sizes.push_back(int(poly.points.size()));
        for (const Point& pt : poly.points) {
            float x = float(unscale<double>(pt.x()));
            float y = float(unscale<double>(pt.y()));
            flat_pts.push_back(Vec3f(x, y, 0.f));
        }
    };

    add_contour(expoly.contour);
    for (const auto& h : expoly.holes)
        add_contour(h);

    int total_pts = int(flat_pts.size());
    int base0 = int(result.vertices.size());

    // Bottom ring (z = 0)
    for (int i = 0; i < total_pts; ++i)
        result.vertices.push_back(flat_pts[i]);
    // Top ring (z = height)
    for (int i = 0; i < total_pts; ++i)
        result.vertices.push_back(Vec3f(flat_pts[i].x(), flat_pts[i].y(), float(height)));

    // --- Phase 2: Side walls ---
    for (size_t ci = 0; ci < contour_starts.size(); ++ci) {
        int start = contour_starts[ci];
        int n     = contour_sizes[ci];
        if (n < 3) continue;

        for (int i = 0; i < n; ++i) {
            int j  = (i + 1) % n;
            int b0 = base0 + start + i;
            int b1 = base0 + start + j;
            int t0 = base0 + total_pts + start + i;
            int t1 = base0 + total_pts + start + j;
            result.indices.push_back({b0, b1, t1});
            result.indices.push_back({b0, t1, t0});
        }
    }

    // --- Phase 3: Top and bottom caps ---
    // Tessellate the ExPolygon, then snap each returned vertex to the
    // nearest shared vertex to avoid duplicate-vertex precision issues.
    {
        ExPolygon ep = expoly;
        ep.contour.make_counter_clockwise();
        for (auto& hole : ep.holes)
            hole.make_clockwise();

        // Build a spatial hash for O(1) vertex lookup.
        // Key: quantised (x, y) packed into int64_t.  The quantisation
        // grid (0.001 mm) is finer than any rounding difference between
        // the tessellator and our canonical vertices.
        auto quantise = [](float v) -> int32_t { return int32_t(std::round(v * 1000.f)); };
        auto pack_key = [&](float x, float y) -> int64_t {
            return (int64_t(quantise(x)) << 32) | (int64_t(quantise(y)) & 0xFFFFFFFF);
        };

        std::unordered_map<int64_t, int> pt_index;
        pt_index.reserve(total_pts);
        for (int i = 0; i < total_pts; ++i)
            pt_index[pack_key(flat_pts[i].x(), flat_pts[i].y())] = i;

        // Look up the shared vertex index for a tessellated point.
        // z_offset selects bottom ring (0) or top ring (total_pts).
        auto find_nearest = [&](float x, float y, int z_offset) -> int {
            auto it = pt_index.find(pack_key(x, y));
            if (it != pt_index.end())
                return base0 + z_offset + it->second;
            // Fallback: linear scan (should not happen)
            int   best_idx  = base0 + z_offset;
            float best_dist = std::numeric_limits<float>::max();
            for (int i = 0; i < total_pts; ++i) {
                float dx = flat_pts[i].x() - x;
                float dy = flat_pts[i].y() - y;
                float d  = dx * dx + dy * dy;
                if (d < best_dist) { best_dist = d; best_idx = base0 + z_offset + i; }
            }
            return best_idx;
        };

        // Bottom face (flip = true for outward-facing-down normals)
        auto bot_tris = triangulate_expolygon_3d(ep, 0.0, true);
        for (size_t i = 0; i + 2 < bot_tris.size(); i += 3) {
            int i0 = find_nearest(float(bot_tris[i].x()),     float(bot_tris[i].y()),     0);
            int i1 = find_nearest(float(bot_tris[i + 1].x()), float(bot_tris[i + 1].y()), 0);
            int i2 = find_nearest(float(bot_tris[i + 2].x()), float(bot_tris[i + 2].y()), 0);
            result.indices.push_back({i0, i1, i2});
        }

        // Top face (flip = false for outward-facing-up normals)
        auto top_tris = triangulate_expolygon_3d(ep, 0.0, false);
        for (size_t i = 0; i + 2 < top_tris.size(); i += 3) {
            int i0 = find_nearest(float(top_tris[i].x()),     float(top_tris[i].y()),     total_pts);
            int i1 = find_nearest(float(top_tris[i + 1].x()), float(top_tris[i + 1].y()), total_pts);
            int i2 = find_nearest(float(top_tris[i + 2].x()), float(top_tris[i + 2].y()), total_pts);
            result.indices.push_back({i0, i1, i2});
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

    double inner_r = gap_width / 2.0;       // fillet radius for slot left ends
    double outer_r = arm_thickness / 2.0;    // fillet radius for arm tips (right side)
    double spine_r = arm_thickness - 0.5;    // fillet radius for spine corners (left side)

    double left_x  = -width / 2.0;          // left edge of bounding box
    double spine_x = left_x + arm_thickness; // right edge of spine (= left edge of slots)
    double bot_y   = -depth / 2.0;           // bottom of bounding box
    double top_y   =  depth / 2.0;           // top of bounding box

    // The cross-section is built as a SINGLE polygon (no holes) by tracing
    // the entire serpentine boundary CCW.  After each arm tip arc, the
    // contour dips into the slot gap — along the bottom of the slot, around
    // the left semicircle (into the spine), then back along the top — before
    // continuing to the next arm.  This avoids polygon-with-holes
    // tessellation artifacts that occur when hole edges coincide with outer
    // contour edges.
    //
    // For 3 arms the path is:
    //   spine bottom-left arc → arm 0 bottom → arm 0 tip arc →
    //   slot 0 bottom → slot 0 left semicircle → slot 0 top →
    //   arm 1 bottom → arm 1 tip arc →
    //   slot 1 bottom → slot 1 left semicircle → slot 1 top →
    //   arm 2 bottom → arm 2 tip arc →
    //   spine top-left arc → (closes back to start)

    double slot_left_cx = spine_x + inner_r;         // left semicircle centre X
    double slot_right_x = width / 2.0 - outer_r;     // arm tip arc centre X

    Polygon outer;

    // Bottom-left spine corner (180° → 270°)
    append_arc(outer, left_x + spine_r, bot_y + spine_r, spine_r, M_PI, 3.0 * M_PI / 2.0);

    for (int i = 0; i < num_arms; ++i) {
        double yb = bot_y + i * (arm_thickness + gap_width);
        double yt = yb + arm_thickness;
        double arm_cy = (yb + yt) / 2.0;

        // Straight bottom edge of arm, then semicircular tip (-90° → +90°)
        outer.points.push_back(Point(coord_t(scale_(slot_right_x)), coord_t(scale_(yb))));
        append_arc(outer, slot_right_x, arm_cy, outer_r, -M_PI / 2.0, M_PI / 2.0);

        if (i < num_arms - 1) {
            // Dip into the slot gap between arm i and arm i+1.
            double slot_cy = yt + gap_width / 2.0;
            double next_yb = yt + gap_width;

            // Bottom of slot (= top of arm i): right → left
            outer.points.push_back(Point(coord_t(scale_(slot_left_cx)), coord_t(scale_(yt))));

            // Left semicircle: -90° → +90° the LONG way through 180°,
            // i.e. -90° → -270° so the arc curves left into the spine.
            append_arc(outer, slot_left_cx, slot_cy, inner_r, -M_PI / 2.0, -3.0 * M_PI / 2.0);

            // Top of slot (= bottom of arm i+1): left → right
            outer.points.push_back(Point(coord_t(scale_(slot_right_x)), coord_t(scale_(next_yb))));
        }
    }

    // Top-left spine corner (90° → 180°)
    append_arc(outer, left_x + spine_r, top_y - spine_r, spine_r, M_PI / 2.0, M_PI);
    // Left edge closes implicitly back to the start point.

    ExPolygon expoly;
    expoly.contour = outer;

    return extrude_expolygon(expoly, height);
}

// ---------------------------------------------------------------------------
// Temperature Tower
// ---------------------------------------------------------------------------
// Each tier is a 79×10×10 mm block with test features: 45°/35° overhangs,
// a central bridge cutout with small and large cones, vertical and horizontal
// holes, a surface protrusion bar, and a raised block-letter temperature label.
// Tiers are stacked on a 89.3×20×1 mm base plate.  Geometry constants below
// are in millimetres and match the original Python temp_model.py.

// Base plate dimensions
static constexpr double BASE_LENGTH     = 89.3;
static constexpr double BASE_WIDTH      = 20.0;
static constexpr double BASE_HEIGHT     = TEMP_TOWER_BASE_HEIGHT;

// Tier block dimensions
static constexpr double TIER_LENGTH     = 79.0;
static constexpr double TIER_WIDTH      = 10.0;
static constexpr double TIER_HEIGHT     = TEMP_TOWER_TIER_HEIGHT;

// Overhang wedge X-extents (left = 45°, right = 35°)
static constexpr double OVERHANG_45_X   = 10.0;
static constexpr double OVERHANG_35_X   = 14.281;  // tan(35°) × TIER_HEIGHT

// Central rectangular cutout (bridge test area)
static constexpr double CUTOUT_LENGTH   = 30.0;
static constexpr double CUTOUT_HEIGHT   = 9.0;
static constexpr double CUTOUT_OFFSET   = 15.0;    // X offset from tier origin

// Cones inside the cutout (stringing/detail test)
static constexpr double CONE_HEIGHT     = 5.0;
static constexpr double SM_CONE_DIAM    = 3.0;
static constexpr double SM_CONE_OFFSET  = 5.0;     // from cutout start
static constexpr double LG_CONE_DIAM    = 5.0;
static constexpr double LG_CONE_OFFSET  = 25.0;    // from cutout start

// Vertical and horizontal holes (bridging/overhang test)
static constexpr double HOLE_DIAM       = 3.0;
static constexpr double HOLE_45_OFFSET  = 3.671;   // near 45° overhang
static constexpr double HOLE_35_OFFSET  = 75.0;    // near 35° overhang
static constexpr double HORIZ_HOLE_LEN  = 5.0;     // horizontal hole length

// Surface protrusion bar (layer adhesion test)
static constexpr double PROTRUSION_LENGTH = 16.0;
static constexpr double PROTRUSION_HEIGHT = 0.7;
static constexpr double PROTRUSION_DEPTH  = 0.5;
static constexpr double TEST_CUTOUT_H_OFFSET = 47.0;
static constexpr double TEST_CUTOUT_V_OFFSET = 0.3;

// Block-letter temperature label (raised on back face)
static constexpr double TEMP_LABEL_SIZE     = 6.0;   // mm font height
static constexpr double TEMP_LABEL_DEPTH    = 0.6;   // mm label thickness
static constexpr double TEMP_LABEL_V_OFFSET = 5.0;   // mm from tier bottom
static constexpr double TEMP_LABEL_H_OFFSET = 25.0;  // mm from tier right edge

/// Build a single tier with all test features at local origin.
/// temperature > 0 adds a raised block-letter label on the back face.
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
    // Base plate centred at XY origin
    auto tower = its_make_cube(BASE_LENGTH, BASE_WIDTH, BASE_HEIGHT);
    its_translate(tower, Vec3f(float(-BASE_LENGTH / 2.0), float(-BASE_WIDTH / 2.0), 0.f));

    // Stack tiers from bottom (hottest) to top (coolest)
    for (int i = 0; i < num_tiers; ++i) {
        int temperature = start_temp - i * temp_step;
        auto tier = make_tier(temperature);

        // Centre tier on base plate, stack at correct Z height
        its_translate(tier, Vec3f(
            float(-TIER_LENGTH / 2.0),
            float(-TIER_WIDTH / 2.0),
            float(BASE_HEIGHT + i * TIER_HEIGHT)));

        try {
            MeshBoolean::cgal::plus(tower, tier);
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(warning) << "CalibrationModels: CGAL union failed for tier "
                                       << i << ": " << e.what() << " — falling back to merge";
            its_merge(tower, tier);
        } catch (...) {
            BOOST_LOG_TRIVIAL(warning) << "CalibrationModels: CGAL union failed for tier "
                                       << i << " — falling back to merge";
            its_merge(tower, tier);
        }
    }

    // Rotate 180° around Z so temperature labels face the front (-Y)
    Transform3d rot = Transform3d::Identity();
    rot.rotate(Eigen::AngleAxisd(M_PI, Vec3d::UnitZ()));
    its_transform(tower, rot);

    return tower;
}

// ---------------------------------------------------------------------------
// Pressure Advance Chevron Pattern
// ---------------------------------------------------------------------------
// Generates a single V-shaped chevron inside a rectangular frame for
// pressure advance calibration.  The model is extruded to
// num_layers × layer_height.  Each group of layers gets a different PA
// value via per-layer custom G-code inserted by the dialog.
//
// Geometry (viewed from above, chevron pointing right):
//
//        outer tip
//       /         \
//      /   inner   \       Each arm is wall_thickness wide.
//     /    notch    \
//    /_______________\
//    arm endpoint      arm endpoint

/// Compute the 6-vertex polygon for a single thick chevron (V-shape).
/// tip_x: X position of the chevron tip.  The chevron points right (+X).
/// Returns vertices CCW suitable for extrusion.
static std::vector<Vec2d> chevron_outline(
    double tip_x, double arm_length, double corner_angle_deg, double wall_thickness)
{
    double half = corner_angle_deg * M_PI / 360.0;  // half angle in radians
    double cos_a = std::cos(half);
    double sin_a = std::sin(half);
    double hw = wall_thickness / 2.0;

    // Centre-line arm endpoint (left of tip)
    double lx = tip_x - arm_length * cos_a;
    double ly = arm_length * sin_a;

    // Outer and inner tip X (intersection of offset arm edges)
    double outer_tip_x = tip_x + hw / sin_a;
    double inner_tip_x = tip_x - hw / sin_a;

    // Arm endpoint offsets (perpendicular to arm direction)
    double top_outer_lx = lx + hw * sin_a;
    double top_outer_ly = ly + hw * cos_a;
    double top_inner_lx = lx - hw * sin_a;
    double top_inner_ly = ly - hw * cos_a;

    // Bottom arm (mirror Y)
    double bot_inner_lx = lx - hw * sin_a;
    double bot_inner_ly = -(ly - hw * cos_a);
    double bot_outer_lx = lx + hw * sin_a;
    double bot_outer_ly = -(ly + hw * cos_a);

    // CCW winding
    return {
        {outer_tip_x,  0.0},           // outer tip (rightmost)
        {top_outer_lx, top_outer_ly},   // outer top-left
        {top_inner_lx, top_inner_ly},   // inner top-left
        {inner_tip_x,  0.0},           // inner tip (notch)
        {bot_inner_lx, bot_inner_ly},   // inner bottom-left
        {bot_outer_lx, bot_outer_ly},   // outer bottom-left
    };
}

/// Simple prism extrusion of a 2D polygon (no holes).
/// Vertices are in XY, extruded from z=0 to z=height.
static indexed_triangle_set extrude_polygon(const std::vector<Vec2d>& pts, double height)
{
    indexed_triangle_set result;
    int n = int(pts.size());
    if (n < 3) return result;

    // Bottom and top vertex rings
    for (int i = 0; i < n; ++i)
        result.vertices.push_back(Vec3f(float(pts[i].x()), float(pts[i].y()), 0.f));
    for (int i = 0; i < n; ++i)
        result.vertices.push_back(Vec3f(float(pts[i].x()), float(pts[i].y()), float(height)));

    // Side quads
    for (int i = 0; i < n; ++i) {
        int j  = (i + 1) % n;
        int b0 = i, b1 = j, t0 = n + i, t1 = n + j;
        result.indices.push_back({b0, b1, t1});
        result.indices.push_back({b0, t1, t0});
    }

    // Bottom cap (fan from vertex 0, reversed winding for downward normal)
    for (int i = 1; i + 1 < n; ++i)
        result.indices.push_back({0, i + 1, i});

    // Top cap (fan from vertex n, normal winding for upward normal)
    for (int i = 1; i + 1 < n; ++i)
        result.indices.push_back({n, n + i, n + i + 1});

    return result;
}

indexed_triangle_set make_pa_pattern(
    int    num_layers,
    double layer_height,
    double corner_angle,
    double arm_length,
    double wall_thickness)
{
    double height = num_layers * layer_height;
    double half   = corner_angle * M_PI / 360.0;
    double sin_a  = std::sin(half);
    double cos_a  = std::cos(half);
    double hw     = wall_thickness / 2.0;

    // Single chevron centred at X = 0
    auto verts  = chevron_outline(0.0, arm_length, corner_angle, wall_thickness);
    auto result = extrude_polygon(verts, height);

    // Rectangular frame enclosing the chevron (1 layer tall)
    {
        double arm_lx = -arm_length * cos_a;
        double x_min  = arm_lx - hw * sin_a - 1.0;  // small margin
        double x_max  = hw / sin_a + 1.0;

        double y_extent = arm_length * sin_a + hw * cos_a;
        double y_min = -y_extent - 1.0;
        double y_max =  y_extent + 1.0;

        double frame_h = layer_height;  // 1 layer tall

        // Outer box
        auto outer = its_make_cube(x_max - x_min, y_max - y_min, frame_h);
        its_translate(outer, Vec3f(float(x_min), float(y_min), 0.f));

        // Inner cutout
        double w = wall_thickness;
        auto inner = its_make_cube(
            x_max - x_min - 2 * w, y_max - y_min - 2 * w, frame_h + 2.0);
        its_translate(inner, Vec3f(float(x_min + w), float(y_min + w), -1.f));

        MeshBoolean::cgal::minus(outer, inner);
        its_merge(result, outer);
    }

    return result;
}

// ---------------------------------------------------------------------------
// Retraction Calibration Towers
// ---------------------------------------------------------------------------

indexed_triangle_set make_retraction_towers(
    double height, double diameter, double spacing)
{
    double radius = diameter / 2.0;

    // Rectangular base: spans both towers with margin
    static constexpr double BASE_HEIGHT  = 1.0;  // mm
    static constexpr double BASE_MARGIN  = 5.0;  // mm padding around towers
    double base_width  = spacing + diameter + 2.0 * BASE_MARGIN;
    double base_depth  = diameter + 2.0 * BASE_MARGIN;

    auto base = its_make_cube(base_width, base_depth, BASE_HEIGHT);
    its_translate(base, Vec3f(float(-base_width / 2.0), float(-base_depth / 2.0), 0.f));

    // Towers sit on top of the base
    double fa = 2.0 * M_PI / 64.0;
    auto tower1 = its_make_cylinder(radius, height - BASE_HEIGHT, fa);
    its_translate(tower1, Vec3f(float(-spacing / 2.0), 0.f, float(BASE_HEIGHT)));

    auto tower2 = its_make_cylinder(radius, height - BASE_HEIGHT, fa);
    its_translate(tower2, Vec3f(float(spacing / 2.0), 0.f, float(BASE_HEIGHT)));

    its_merge(base, tower1);
    its_merge(base, tower2);
    return base;
}

} // namespace Slic3r
