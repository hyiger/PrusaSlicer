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
#include "libslic3r/GCode/CalibrationFlowPostProcessor.hpp"

#include <boost/filesystem.hpp>

#include <clocale>
#include <cstdio>
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

// Writes a temp G-code file. By default it prepends the calibration marker
// comment so the post-processor recognises the file as a calibration print;
// pass with_marker=false to exercise the marker-absent (no-op) path or to
// build a binary blob whose first bytes must stay intact.
static std::string write_tmp_gcode(const std::string& content, bool with_marker = true)
{
    auto p = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("retcal-%%%%-%%%%.gcode");
    std::ofstream f(p.string(), std::ios::binary);
    if (with_marker)
        f << "; " << calibration_retraction_marker() << "\n";
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

TEST_CASE("calibration retraction: no-op when calibration marker absent", "[calibration]")
{
    // The post_process hook persists in the print preset, so the post-processor
    // runs for every slice. A normal print (no calibration marker) must be
    // passed through completely untouched.
    const std::string input =
        ";Z:1.2\n"
        "G1 E-1.6 F2700\n"
        "G1 X10 Y10 F21000\n"
        "G1 E1.6 F1500\n";
    auto url  = make_calibration_retraction_url(0.7, {{2.0, 0.0}});
    auto path = write_tmp_gcode(input, /*with_marker=*/false);
    CHECK_FALSE(run_calibration_retraction_post_processor(url, path));  // no-op
    auto out = slurp(path);
    boost::filesystem::remove(path);
    CHECK(out == input);  // byte-identical — untouched
}

TEST_CASE("calibration retraction: rewrites only between the two markers", "[calibration]")
{
    // Retracts before the first marker (start G-code) and after the second
    // (end G-code) are nozzle priming / cleanup — they must be left intact.
    // The closing marker arrives mid-pair (between a layer-change retract and
    // its recovery, as in real G-code), so the exit is deferred until that
    // recovery is rewritten — otherwise the top band would have a retract
    // rewritten but its recovery left at the sentinel value.
    const std::string mk = std::string("; ") + calibration_retraction_marker() + "\n";
    const std::string input =
        ";Z:0.5\n"
        "G1 E-9 F2700\n"        // start G-code retract — before first marker
        + mk +                  // opening marker
        ";Z:1.2\n"
        "G1 E-9 F2700\n"        // body retract — rewritten, pair opens
        "G1 X5 Y5 F21000\n"     // travel
        + mk +                  // closing marker — arrives mid-pair, exit deferred
        ";Z:1.4\n"
        "G1 E9 F1500\n"         // body recovery — still rewritten, pair closes
        "G1 E-9 F2700\n";       // end G-code retract — body already exited, left verbatim

    auto url  = make_calibration_retraction_url(0.7, {{2.0, 0.0}});
    auto path = write_tmp_gcode(input, /*with_marker=*/false);  // input carries its own markers
    REQUIRE(run_calibration_retraction_post_processor(url, path));
    auto out = slurp(path);
    boost::filesystem::remove(path);

    // Body retract and its (post-closing-marker) recovery are both rewritten.
    CHECK(out.find("G1 E-0.0000 F2700 ; r z=1.20") != std::string::npos);
    CHECK(out.find("G1 E0.0000 F1500 ; R z=1.40")  != std::string::npos);
    // Start and end retracts left verbatim — exactly two unchanged moves.
    size_t count = 0, pos = 0;
    while ((pos = out.find("G1 E-9 F2700\n", pos)) != std::string::npos) {
        ++count;
        pos += 1;
    }
    CHECK(count == 2);
}

TEST_CASE("calibration retraction: refuses binary G-code input", "[calibration]")
{
    // A file starting with "GCDE" (bgcode magic) is binary G-code. When it
    // carries the calibration marker the rewriter must refuse loudly instead
    // of scribbling over a binarized payload.
    std::string blob = std::string("GCDE\x01\x00\x00\x00\x01\x00", 10)
                     + "\n; " + calibration_retraction_marker() + "\n";
    auto path = write_tmp_gcode(blob, /*with_marker=*/false);
    auto url  = make_calibration_retraction_url(0.7, {{2.0, 0.0}});
    CHECK_THROWS_WITH(run_calibration_retraction_post_processor(url, path),
                      Catch::Matchers::ContainsSubstring("binary G-code"));
    boost::filesystem::remove(path);
}

TEST_CASE("calibration retraction: leaves unpaired positive-E moves untouched", "[calibration]")
{
    // Standalone prime/unload commands (default profiles emit `G1 E2` /
    // `G1 E10` in start/end G-code) have no preceding retract — they must be
    // passed through verbatim, not rewritten into tiny deretracts.
    const std::string input =
        ";Z:1.2\n"
        "G1 E2 F2400\n"          // prime — no preceding retract
        "G1 E-1.6 F2700\n"       // retract -> pending
        "G1 X10 Y10 F21000\n"    // travel
        "G1 E1.6 F1500\n"        // recovery — paired, rewritten
        "G1 E10 F2400\n";        // unload — pending already cleared

    auto url  = make_calibration_retraction_url(0.7, {{2.0, 0.0}});
    auto path = write_tmp_gcode(input);
    REQUIRE(run_calibration_retraction_post_processor(url, path));
    auto out = slurp(path);
    boost::filesystem::remove(path);

    // Unpaired prime/unload moves preserved byte-for-byte
    CHECK(out.find("G1 E2 F2400\n") != std::string::npos);
    CHECK(out.find("G1 E10 F2400\n") != std::string::npos);
    // The retract (z=1.2 -> band 0 = 0.0) and its paired recovery are rewritten
    CHECK(out.find("G1 E-0.0000 F2700 ; r z=1.20") != std::string::npos);
    CHECK(out.find("G1 E0.0000 F1500 ; R z=1.20") != std::string::npos);
}

TEST_CASE("calibration retraction: refuses absolute-E (M82) input", "[calibration]")
{
    // In absolute-E mode the sign-based retract/recovery classifier is invalid.
    // The rewriter must refuse once it sees M82, not corrupt the print.
    const std::string input =
        "M82\n"                     // absolute extrusion
        ";Z:1.2\n"
        "G1 E-1.6 F2700\n";
    auto url  = make_calibration_retraction_url(0.7, {{2.0, 0.0}});
    auto path = write_tmp_gcode(input);
    CHECK_THROWS_WITH(run_calibration_retraction_post_processor(url, path),
                      Catch::Matchers::ContainsSubstring("absolute E"));
    boost::filesystem::remove(path);
}

TEST_CASE("calibration retraction: M83 after M82 re-enables rewriting", "[calibration]")
{
    // Mode tracking must be live: an M82 then M83 leaves us in relative mode.
    const std::string input =
        "M82\n"
        "M83\n"                     // back to relative
        ";Z:1.2\n"
        "G1 E-1.6 F2700\n";
    auto url  = make_calibration_retraction_url(0.7, {{2.0, 0.0}});
    auto path = write_tmp_gcode(input);
    REQUIRE(run_calibration_retraction_post_processor(url, path));
    auto out = slurp(path);
    boost::filesystem::remove(path);
    // z=1.2 → band 0 = 0.0
    CHECK(out.find("G1 E-0.0000") != std::string::npos);
}

TEST_CASE("calibration retraction: URL uses locale-invariant decimals", "[calibration]")
{
    // The URL is comma-delimited; a comma decimal separator (de_DE etc.) would
    // make it ambiguous. make_calibration_retraction_url() and the rewrite path
    // must always emit '.' decimals regardless of the process locale.
    const std::string saved = std::setlocale(LC_NUMERIC, nullptr);
    struct Restore {
        std::string s;
        ~Restore() { std::setlocale(LC_NUMERIC, s.c_str()); }
    } restore{saved};

    bool comma_locale = false;
    for (const char* loc : {"de_DE.UTF-8", "de_DE", "fr_FR.UTF-8", "nl_NL.UTF-8"}) {
        if (std::setlocale(LC_NUMERIC, loc)) {
            char probe[8];
            std::snprintf(probe, sizeof(probe), "%.1f", 1.5);
            if (std::string(probe) == "1,5") { comma_locale = true; break; }
        }
    }

    auto url = make_calibration_retraction_url(0.7, {{2.0, 0.1}, {3.0, 0.2}});
    CHECK(url.find("base=0.7000") != std::string::npos);
    CHECK(url.find("2.0000:0.1000") != std::string::npos);
    CHECK(url.find("0,7000") == std::string::npos);  // never a comma decimal

    if (comma_locale) {
        // The rewrite path formats E values with snprintf too — verify those
        // also stay '.' under a comma locale, or the G-code would be invalid.
        const std::string input =
            ";Z:1.2\nG1 E-9 F2700\nG1 X1 Y1 F900\nG1 E9 F900\n";
        auto path = write_tmp_gcode(input);
        REQUIRE(run_calibration_retraction_post_processor(url, path));
        auto out = slurp(path);
        boost::filesystem::remove(path);
        CHECK(out.find("G1 E-0.1000") != std::string::npos);
        CHECK(out.find("0,1000") == std::string::npos);
    }
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

// ---------------------------------------------------------------------------
// Flow Rate (YOLO) Calibration Post-Processor
// ---------------------------------------------------------------------------

// Writes a temp G-code file, prepending the flow calibration marker by default
// so the post-processor recognises the file as a calibration print.
static std::string write_tmp_flow_gcode(const std::string& content, bool with_marker = true)
{
    auto p = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("flowcal-%%%%-%%%%.gcode");
    std::ofstream f(p.string(), std::ios::binary);
    if (with_marker)
        f << "; " << calibration_flow_marker() << "\n";
    f << content;
    f.close();
    return p.string();
}

TEST_CASE("calibration flow: URL round-trip", "[calibration]")
{
    std::vector<std::pair<std::string, double>> objs = {
        {"-.05", 0.95}, {"0", 1.0}, {".05", 1.05}};
    auto url = make_calibration_flow_url(objs);
    CHECK(is_calibration_flow_url(url));
    CHECK(url.find("-.05:0.95000") != std::string::npos);
    CHECK(url.find("0:1.00000") != std::string::npos);
    CHECK(url.find(".05:1.05000") != std::string::npos);

    CHECK_FALSE(is_calibration_flow_url("/usr/local/bin/my_script.py"));
    CHECK_FALSE(is_calibration_flow_url("::builtin::retraction_calibration?base=0.7"));
}

TEST_CASE("calibration flow: scales deposition E per object via M486", "[calibration]")
{
    // Header maps S0->".05" and S1->"-.05" — deliberately NOT in offset order,
    // to prove the post-processor keys on the object NAME, not the M486 id
    // (ids are assigned in pointer order and need not match the pad order).
    const std::string input =
        "M486 S0\n"
        "M486 A.05\n"
        "M486 S-1\n"
        "M486 S1\n"
        "M486 A-.05\n"
        "M486 S-1\n"
        "G1 X235 E5 F500\n"          // purge, outside any object -> untouched
        "M486 S1\n"                  // object "-.05" -> factor 0.95
        "G1 X10 Y10 E1 F1500\n"      // deposition -> 0.95000
        "G1 E-0.25 F1500\n"          // retract (pure E) -> untouched
        "G1 X20 Y20 F21000\n"        // travel (no E) -> untouched
        "M486 S-1\n"
        "M486 S0\n"                  // object ".05" -> factor 1.05
        "G1 X10 Y10 E1 F1500\n"      // deposition -> 1.05000
        "G3 X12 Y12 I1 J1 E.5 F1800\n"; // arc deposition -> 0.52500

    std::vector<std::pair<std::string, double>> objs = {{"-.05", 0.95}, {".05", 1.05}};
    auto url  = make_calibration_flow_url(objs);
    auto path = write_tmp_flow_gcode(input);
    REQUIRE(run_calibration_flow_post_processor(url, path));
    auto out = slurp(path);
    boost::filesystem::remove(path);

    // Pad "-.05": deposition scaled 0.95, retract & travel untouched.
    CHECK(out.find("G1 X10 Y10 E0.95000 F1500") != std::string::npos);
    CHECK(out.find("G1 E-0.25 F1500\n")          != std::string::npos);
    CHECK(out.find("G1 X20 Y20 F21000\n")        != std::string::npos);
    // Pad ".05": deposition + arc scaled 1.05.
    CHECK(out.find("G1 X10 Y10 E1.05000 F1500")  != std::string::npos);
    CHECK(out.find("G3 X12 Y12 I1 J1 E0.52500 F1800") != std::string::npos);
    // Purge line outside any object is never scaled.
    CHECK(out.find("G1 X235 E5 F500\n")          != std::string::npos);
}

TEST_CASE("calibration flow: center pad (factor 1.0) is byte-identical", "[calibration]")
{
    const std::string input =
        "M486 S0\n"
        "M486 A0\n"
        "M486 S-1\n"
        "M486 S0\n"
        "G1 X10 Y10 E1 F1500\n"
        "G1 X20 Y20 E.5 F1500\n"
        "M486 S-1\n";
    auto url  = make_calibration_flow_url({{"0", 1.0}});
    auto path = write_tmp_flow_gcode(input);
    REQUIRE(run_calibration_flow_post_processor(url, path));
    auto out = slurp(path);
    boost::filesystem::remove(path);
    // Marker + unchanged body; the deposition lines are untouched.
    CHECK(out.find("G1 X10 Y10 E1 F1500\n") != std::string::npos);
    CHECK(out.find("G1 X20 Y20 E.5 F1500\n") != std::string::npos);
}

TEST_CASE("calibration flow: no-op when marker absent", "[calibration]")
{
    const std::string input =
        "M486 S0\n"
        "M486 A-.05\n"
        "M486 S-1\n"
        "M486 S0\n"
        "G1 X10 Y10 E1 F1500\n"
        "M486 S-1\n";
    auto url  = make_calibration_flow_url({{"-.05", 0.95}});
    auto path = write_tmp_flow_gcode(input, /*with_marker=*/false);
    CHECK_FALSE(run_calibration_flow_post_processor(url, path));  // no-op
    auto out = slurp(path);
    boost::filesystem::remove(path);
    CHECK(out == input);  // byte-identical — untouched
}

TEST_CASE("calibration flow: unknown object name passes through unscaled", "[calibration]")
{
    // A body object whose name isn't in the factor table must not be scaled.
    const std::string input =
        "M486 S0\n"
        "M486 Asomething-else\n"
        "M486 S-1\n"
        "M486 S0\n"
        "G1 X10 Y10 E1 F1500\n"
        "M486 S-1\n";
    auto url  = make_calibration_flow_url({{"-.05", 0.95}});
    auto path = write_tmp_flow_gcode(input);
    REQUIRE(run_calibration_flow_post_processor(url, path));
    auto out = slurp(path);
    boost::filesystem::remove(path);
    CHECK(out.find("G1 X10 Y10 E1 F1500\n") != std::string::npos);  // unchanged
}

TEST_CASE("calibration flow: Klipper EXCLUDE_OBJECT markers with sanitized names", "[calibration]")
{
    // Klipper flavor rewrites object-label names (LabelObjects::init replaces
    // '-' and '.' with '_'), so the dialog's "-.05"/".05" pads are emitted as
    // "__05"/"_05". The URL still carries the raw names; the post-processor
    // must match them against the rewritten marker names.
    const std::string input =
        "EXCLUDE_OBJECT_START NAME='__05'\n"
        "G1 X1 Y1 E1 F1500\n"
        "EXCLUDE_OBJECT_END NAME='__05'\n"
        "EXCLUDE_OBJECT_START NAME='_05'\n"
        "G1 X2 Y2 E1 F1500\n"
        "EXCLUDE_OBJECT_END NAME='_05'\n";
    auto url  = make_calibration_flow_url({{"-.05", 0.95}, {".05", 1.05}});
    auto path = write_tmp_flow_gcode(input);
    REQUIRE(run_calibration_flow_post_processor(url, path));
    auto out = slurp(path);
    boost::filesystem::remove(path);
    CHECK(out.find("G1 X1 Y1 E0.95000 F1500") != std::string::npos);  // "__05" -> 0.95
    CHECK(out.find("G1 X2 Y2 E1.05000 F1500") != std::string::npos);  // "_05"  -> 1.05
}

TEST_CASE("calibration flow: OctoPrint object comments with id/copy suffix", "[calibration]")
{
    // OctoPrint labeling (what the dialog forces — universal across flavors)
    // emits "; printing object <name> id:<n> copy <m>". The post-processor must
    // strip the " id:.. copy .." suffix to recover the pad name.
    const std::string input =
        "; printing object -.05 id:6 copy 0\n"
        "G1 X1 Y1 E1 F1500\n"
        "; stop printing object -.05 id:6 copy 0\n"
        "; printing object 0 id:8 copy 0\n"
        "G1 X2 Y2 E1 F1500\n"            // center pad -> unchanged
        "; stop printing object 0 id:8 copy 0\n"
        "; printing object .05 id:2 copy 0\n"
        "G1 X3 Y3 E1 F1500\n"
        "; stop printing object .05 id:2 copy 0\n";
    auto url  = make_calibration_flow_url({{"-.05", 0.95}, {"0", 1.0}, {".05", 1.05}});
    auto path = write_tmp_flow_gcode(input);
    REQUIRE(run_calibration_flow_post_processor(url, path));
    auto out = slurp(path);
    boost::filesystem::remove(path);
    CHECK(out.find("G1 X1 Y1 E0.95000 F1500") != std::string::npos);
    CHECK(out.find("G1 X2 Y2 E1 F1500")       != std::string::npos);  // center untouched
    CHECK(out.find("G1 X3 Y3 E1.05000 F1500") != std::string::npos);
}

TEST_CASE("calibration flow: RepRapFirmware single-line M486 header", "[calibration]")
{
    // RRF emits the object definition on ONE line as `M486 S<id> A"<name>"`
    // (Marlin uses two lines). The post-processor must read the inline name
    // so id->factor is populated; otherwise every pad selects factor 1.0.
    const std::string input =
        "M486 S0 A\"-.05\"\n"
        "M486 S-1\n"
        "M486 S1 A\".05\"\n"
        "M486 S-1\n"
        "M486 S0\n"                  // body: object "-.05" -> 0.95
        "G1 X1 Y1 E1 F1500\n"
        "M486 S-1\n"
        "M486 S1\n"                  // body: object ".05" -> 1.05
        "G1 X2 Y2 E1 F1500\n"
        "M486 S-1\n";
    auto url  = make_calibration_flow_url({{"-.05", 0.95}, {".05", 1.05}});
    auto path = write_tmp_flow_gcode(input);
    REQUIRE(run_calibration_flow_post_processor(url, path));
    auto out = slurp(path);
    boost::filesystem::remove(path);
    CHECK(out.find("G1 X1 Y1 E0.95000 F1500") != std::string::npos);
    CHECK(out.find("G1 X2 Y2 E1.05000 F1500") != std::string::npos);
}

TEST_CASE("calibration flow: refuses binary G-code input", "[calibration]")
{
    std::string blob = std::string("GCDE\x01\x00\x00\x00\x01\x00", 10)
                     + "\n; " + calibration_flow_marker() + "\n";
    auto path = write_tmp_flow_gcode(blob, /*with_marker=*/false);
    auto url  = make_calibration_flow_url({{"-.05", 0.95}});
    CHECK_THROWS_WITH(run_calibration_flow_post_processor(url, path),
                      Catch::Matchers::ContainsSubstring("binary G-code"));
    boost::filesystem::remove(path);
}

TEST_CASE("calibration flow: refuses absolute-E (M82) input", "[calibration]")
{
    const std::string input =
        "M82\n"
        "M486 S0\n"
        "M486 A-.05\n"
        "M486 S-1\n"
        "M486 S0\n"
        "G1 X10 Y10 E1 F1500\n";
    auto url  = make_calibration_flow_url({{"-.05", 0.95}});
    auto path = write_tmp_flow_gcode(input);
    CHECK_THROWS_WITH(run_calibration_flow_post_processor(url, path),
                      Catch::Matchers::ContainsSubstring("absolute E"));
    boost::filesystem::remove(path);
}

TEST_CASE("calibration flow: M83 after M82 re-enables scaling", "[calibration]")
{
    const std::string input =
        "M82\n"
        "M83\n"
        "M486 S0\n"
        "M486 A-.05\n"
        "M486 S-1\n"
        "M486 S0\n"
        "G1 X10 Y10 E1 F1500\n";
    auto url  = make_calibration_flow_url({{"-.05", 0.95}});
    auto path = write_tmp_flow_gcode(input);
    REQUIRE(run_calibration_flow_post_processor(url, path));
    auto out = slurp(path);
    boost::filesystem::remove(path);
    CHECK(out.find("G1 X10 Y10 E0.95000 F1500") != std::string::npos);
}

TEST_CASE("calibration flow: URL and rewrite use locale-invariant decimals", "[calibration]")
{
    const std::string saved = std::setlocale(LC_NUMERIC, nullptr);
    struct Restore {
        std::string s;
        ~Restore() { std::setlocale(LC_NUMERIC, s.c_str()); }
    } restore{saved};

    bool comma_locale = false;
    for (const char* loc : {"de_DE.UTF-8", "de_DE", "fr_FR.UTF-8", "nl_NL.UTF-8"}) {
        if (std::setlocale(LC_NUMERIC, loc)) {
            char probe[8];
            std::snprintf(probe, sizeof(probe), "%.1f", 1.5);
            if (std::string(probe) == "1,5") { comma_locale = true; break; }
        }
    }

    auto url = make_calibration_flow_url({{"-.05", 0.95}, {".05", 1.05}});
    CHECK(url.find("-.05:0.95000") != std::string::npos);
    CHECK(url.find("0,95000") == std::string::npos);  // never a comma decimal

    if (comma_locale) {
        const std::string input =
            "M486 S0\nM486 A-.05\nM486 S-1\nM486 S0\nG1 X1 Y1 E1 F900\n";
        auto path = write_tmp_flow_gcode(input);
        REQUIRE(run_calibration_flow_post_processor(url, path));
        auto out = slurp(path);
        boost::filesystem::remove(path);
        CHECK(out.find("G1 X1 Y1 E0.95000 F900") != std::string::npos);
        CHECK(out.find("0,95000") == std::string::npos);
    }
}

TEST_CASE("calibration flow: malformed URL throws", "[calibration]")
{
    auto path = write_tmp_flow_gcode("G1 X1 Y1 E1 F900\n");
    CHECK_THROWS(run_calibration_flow_post_processor(
        "::builtin::flow_calibration", path));  // no query string
    CHECK_THROWS(run_calibration_flow_post_processor(
        "::builtin::flow_calibration?foo=bar", path));  // no objects
    boost::filesystem::remove(path);
}
