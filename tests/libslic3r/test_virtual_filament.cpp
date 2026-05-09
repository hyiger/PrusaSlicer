#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers.hpp>

using Catch::Approx;

#include "libslic3r/VirtualFilament.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace Slic3r;

// ---- VirtualFilament struct ----

TEST_CASE("VirtualFilament default state", "[VirtualFilament]") {
    VirtualFilament vf;
    CHECK(vf.component_a == 1);
    CHECK(vf.component_b == 2);
    CHECK(vf.ratio_a == 1);
    CHECK(vf.ratio_b == 1);
    CHECK(vf.mix_b_percent == 50);
    CHECK(vf.enabled == true);
    CHECK(vf.deleted == false);
    CHECK(vf.custom == false);
    CHECK(vf.manual_pattern.empty());
}

TEST_CASE("VirtualFilament equality", "[VirtualFilament]") {
    VirtualFilament a, b;
    CHECK(a == b);

    b.mix_b_percent = 30;
    CHECK(a != b);
}

// ---- auto_generate ----

TEST_CASE("auto_generate with 2 filaments", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#0000FF"};
    mgr.auto_generate(colours);

    CHECK(mgr.filaments().size() == 1); // C(2,2) = 1 pair
    CHECK(mgr.enabled_count() == 1);
    CHECK(mgr.total_filaments(2) == 3); // 2 physical + 1 virtual

    const auto &vf = mgr.filaments()[0];
    CHECK(vf.component_a == 1);
    CHECK(vf.component_b == 2);
    CHECK(vf.enabled == true);
    CHECK(vf.origin_auto == true);
    CHECK(vf.custom == false);
}

TEST_CASE("auto_generate with 3 filaments", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#00FF00", "#0000FF"};
    mgr.auto_generate(colours);

    CHECK(mgr.filaments().size() == 3); // C(3,2) = 3 pairs
    CHECK(mgr.enabled_count() == 3);
    CHECK(mgr.total_filaments(3) == 6); // 3 physical + 3 virtual
}

TEST_CASE("auto_generate with 4 filaments", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#00FF00", "#0000FF", "#FFFF00"};
    mgr.auto_generate(colours);

    CHECK(mgr.filaments().size() == 6); // C(4,2) = 6 pairs
    CHECK(mgr.enabled_count() == 6);
    CHECK(mgr.total_filaments(4) == 10);
}

TEST_CASE("auto_generate with 1 filament produces no virtual filaments", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000"};
    mgr.auto_generate(colours);

    CHECK(mgr.filaments().empty());
    CHECK(mgr.enabled_count() == 0);
    CHECK(mgr.total_filaments(1) == 1);
}

TEST_CASE("auto_generate with 0 filaments", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {};
    mgr.auto_generate(colours);

    CHECK(mgr.filaments().empty());
    CHECK(mgr.total_filaments(0) == 0);
}

TEST_CASE("auto_generate preserves prior enabled state", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#00FF00", "#0000FF"};
    mgr.auto_generate(colours);

    // Disable the first virtual filament (1+2)
    mgr.filaments()[0].enabled = false;
    CHECK(mgr.enabled_count() == 2);

    // Re-generate — should preserve disabled state
    mgr.auto_generate(colours);
    CHECK(mgr.filaments().size() == 3);
    CHECK(mgr.filaments()[0].enabled == false);
    CHECK(mgr.enabled_count() == 2);
}

// ---- resolve ----

TEST_CASE("resolve returns physical ID unchanged", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#0000FF"};
    mgr.auto_generate(colours);

    // Physical filament IDs (1 and 2) should pass through unchanged.
    CHECK(mgr.resolve(1, 2, 0) == 1);
    CHECK(mgr.resolve(2, 2, 0) == 2);
}

TEST_CASE("resolve alternates components for 1:1 ratio", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#0000FF"};
    mgr.auto_generate(colours);

    // Virtual filament ID = 3 (num_physical=2, first enabled virtual is index 0)
    // Default ratio is 1:1, so it alternates: A, B, A, B, ...
    CHECK(mgr.resolve(3, 2, 0) == 1); // layer 0 -> component_a
    CHECK(mgr.resolve(3, 2, 1) == 2); // layer 1 -> component_b
    CHECK(mgr.resolve(3, 2, 2) == 1); // layer 2 -> component_a
    CHECK(mgr.resolve(3, 2, 3) == 2); // layer 3 -> component_b
}

TEST_CASE("resolve with manual pattern", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#0000FF"};
    mgr.auto_generate(colours);

    // Set a manual pattern: AABB repeating
    mgr.filaments()[0].manual_pattern = "1122";
    mgr.filaments()[0].custom = true;

    CHECK(mgr.resolve(3, 2, 0) == 1); // '1' -> component_a
    CHECK(mgr.resolve(3, 2, 1) == 1); // '1' -> component_a
    CHECK(mgr.resolve(3, 2, 2) == 2); // '2' -> component_b
    CHECK(mgr.resolve(3, 2, 3) == 2); // '2' -> component_b
    CHECK(mgr.resolve(3, 2, 4) == 1); // wraps: '1'
}

TEST_CASE("resolve with 2:1 ratio", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#0000FF"};
    mgr.auto_generate(colours);

    auto &vf = mgr.filaments()[0];
    vf.ratio_a = 2;
    vf.ratio_b = 1;

    // Cycle of 3: A, A, B
    CHECK(mgr.resolve(3, 2, 0) == 1);
    CHECK(mgr.resolve(3, 2, 1) == 1);
    CHECK(mgr.resolve(3, 2, 2) == 2);
    CHECK(mgr.resolve(3, 2, 3) == 1); // wraps
}

TEST_CASE("resolve with ratio_b = 0 always returns component_a", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#0000FF"};
    mgr.auto_generate(colours);

    auto &vf = mgr.filaments()[0];
    vf.ratio_a = 1;
    vf.ratio_b = 0;

    CHECK(mgr.resolve(3, 2, 0) == 1);
    CHECK(mgr.resolve(3, 2, 1) == 1);
    CHECK(mgr.resolve(3, 2, 99) == 1);
}

TEST_CASE("resolve with ratio_a = 0 always returns component_b", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#0000FF"};
    mgr.auto_generate(colours);

    auto &vf = mgr.filaments()[0];
    vf.ratio_a = 0;
    vf.ratio_b = 1;

    CHECK(mgr.resolve(3, 2, 0) == 2);
    CHECK(mgr.resolve(3, 2, 1) == 2);
}

TEST_CASE("resolve with extreme ratio 10:1", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#0000FF"};
    mgr.auto_generate(colours);

    auto &vf = mgr.filaments()[0];
    vf.ratio_a = 10;
    vf.ratio_b = 1;

    // First 10 layers are A, 11th is B
    for (int i = 0; i < 10; ++i)
        CHECK(mgr.resolve(3, 2, i) == 1);
    CHECK(mgr.resolve(3, 2, 10) == 2);
    CHECK(mgr.resolve(3, 2, 11) == 1); // wraps
}

TEST_CASE("resolve with negative layer index is safe", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#0000FF"};
    mgr.auto_generate(colours);

    // Should not crash with negative indices
    unsigned int result = mgr.resolve(3, 2, -1);
    CHECK((result == 1 || result == 2));
    result = mgr.resolve(3, 2, -100);
    CHECK((result == 1 || result == 2));
}

// ---- is_virtual / virtual_index_from_id ----

TEST_CASE("is_virtual correctly identifies virtual IDs", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#00FF00", "#0000FF"};
    mgr.auto_generate(colours);

    CHECK_FALSE(mgr.is_virtual(1, 3));
    CHECK_FALSE(mgr.is_virtual(2, 3));
    CHECK_FALSE(mgr.is_virtual(3, 3));
    CHECK(mgr.is_virtual(4, 3)); // first virtual
    CHECK(mgr.is_virtual(5, 3)); // second virtual
    CHECK(mgr.is_virtual(6, 3)); // third virtual
    CHECK_FALSE(mgr.is_virtual(7, 3)); // out of range
}

TEST_CASE("virtual_index_from_id maps correctly", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#0000FF"};
    mgr.auto_generate(colours);

    CHECK(mgr.virtual_index_from_id(1, 2) == -1); // physical
    CHECK(mgr.virtual_index_from_id(2, 2) == -1); // physical
    CHECK(mgr.virtual_index_from_id(3, 2) == 0);  // first virtual
    CHECK(mgr.virtual_index_from_id(4, 2) == -1); // out of range
}

TEST_CASE("virtual_index_from_id is stable across enable toggle", "[VirtualFilamentManager]") {
    // Regression test: disabling a row must NOT renumber later rows.
    // Painted facets / object-level extruder assignments store numeric IDs
    // and would silently remap otherwise.
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#00FF00", "#0000FF"};
    mgr.auto_generate(colours);

    // Capture baseline mapping (all three auto rows enabled).
    REQUIRE(mgr.virtual_index_from_id(4, 3) == 0); // ID 4 -> row 0 (1+2)
    REQUIRE(mgr.virtual_index_from_id(5, 3) == 1); // ID 5 -> row 1 (1+3)
    REQUIRE(mgr.virtual_index_from_id(6, 3) == 2); // ID 6 -> row 2 (2+3)

    // Disable the first virtual filament (row 0). All IDs must still map
    // to the same rows — disabled-but-not-deleted rows reserve their slot.
    mgr.filaments()[0].enabled = false;
    CHECK(mgr.virtual_index_from_id(4, 3) == 0);
    CHECK(mgr.virtual_index_from_id(5, 3) == 1);
    CHECK(mgr.virtual_index_from_id(6, 3) == 2);

    // Marking a row as deleted (removed) is different: it no longer
    // reserves a slot, and later rows do shift down.
    mgr.filaments()[0].deleted = true;
    CHECK(mgr.virtual_index_from_id(4, 3) == 1);
    CHECK(mgr.virtual_index_from_id(5, 3) == 2);
    CHECK(mgr.virtual_index_from_id(6, 3) == -1);
}

TEST_CASE("total_filaments counts reserved (non-deleted) rows", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#00FF00", "#0000FF"};
    mgr.auto_generate(colours);
    REQUIRE(mgr.total_filaments(3) == 6);

    // Disable — still reserves a slot.
    mgr.filaments()[0].enabled = false;
    CHECK(mgr.total_filaments(3) == 6);
    CHECK(mgr.reserved_count() == 3);
    CHECK(mgr.enabled_count() == 2);

    // Delete — no longer reserves a slot.
    mgr.filaments()[0].deleted = true;
    CHECK(mgr.total_filaments(3) == 5);
    CHECK(mgr.reserved_count() == 2);
}

// ---- serialize / deserialize round-trip ----

TEST_CASE("serialize/deserialize round-trip preserves state", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#00FF00", "#0000FF"};
    mgr.auto_generate(colours);

    // Modify some state
    mgr.filaments()[0].enabled = false;
    mgr.filaments()[1].mix_b_percent = 75;

    std::string serialized = mgr.serialize();
    REQUIRE_FALSE(serialized.empty());

    // Deserialize into a fresh manager
    VirtualFilamentManager mgr2;
    mgr2.auto_generate(colours);
    mgr2.deserialize(serialized, colours);

    CHECK(mgr2.filaments().size() == mgr.filaments().size());
    CHECK(mgr2.filaments()[0].enabled == false);
    // Verify the mix_b_percent was preserved for a custom row
    // (auto rows get their mix_b_percent from the serialized data)
}

TEST_CASE("serialize produces non-empty string", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#0000FF"};
    mgr.auto_generate(colours);

    std::string s = mgr.serialize();
    CHECK_FALSE(s.empty());
    // Should contain semicolons for row separators (only with 1 row, no semicolons needed)
    // But should contain comma-separated fields
    CHECK(s.find(',') != std::string::npos);
}

TEST_CASE("deserialize with empty string is no-op", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#0000FF"};
    mgr.auto_generate(colours);

    size_t before = mgr.filaments().size();
    mgr.deserialize("", colours);
    CHECK(mgr.filaments().size() == before);
}

TEST_CASE("deserialize with invalid data is safe", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#0000FF"};
    mgr.auto_generate(colours);

    // Should not crash
    mgr.deserialize("garbage;invalid;data", colours);
    mgr.deserialize(";;;", colours);
    mgr.deserialize(",,,", colours);
}

TEST_CASE("serialize round-trip with multiple pairs", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#00FF00", "#0000FF", "#FFFF00"};
    mgr.auto_generate(colours);

    // Disable a few
    mgr.filaments()[1].enabled = false;
    mgr.filaments()[3].enabled = false;
    mgr.filaments()[3].deleted = true;

    std::string serialized = mgr.serialize();

    VirtualFilamentManager mgr2;
    mgr2.auto_generate(colours);
    mgr2.deserialize(serialized, colours);

    CHECK(mgr2.filaments()[1].enabled == false);
    CHECK(mgr2.filaments()[3].deleted == true);
    CHECK(mgr2.filaments()[3].enabled == false);
    CHECK(mgr2.enabled_count() == mgr.enabled_count());
}

// ---- add_custom ----

TEST_CASE("add_custom creates a custom virtual filament", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#00FF00", "#0000FF"};
    mgr.auto_generate(colours);

    size_t before = mgr.filaments().size();
    mgr.add_custom(1, 3, 30, colours);

    CHECK(mgr.filaments().size() == before + 1);
    const auto &custom = mgr.filaments().back();
    CHECK(custom.component_a == 1);
    CHECK(custom.component_b == 3);
    CHECK(custom.mix_b_percent == 30);
    CHECK(custom.custom == true);
    CHECK(custom.origin_auto == false);
    CHECK(custom.enabled == true);
}

TEST_CASE("add_custom with same components adjusts component_b", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#00FF00"};
    mgr.auto_generate(colours);

    mgr.add_custom(1, 1, 50, colours);
    const auto &custom = mgr.filaments().back();
    CHECK(custom.component_a != custom.component_b);
}

// ---- remove_physical_filament ----

TEST_CASE("remove_physical_filament removes affected pairs", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#00FF00", "#0000FF"};
    mgr.auto_generate(colours);

    CHECK(mgr.filaments().size() == 3); // 1+2, 1+3, 2+3

    mgr.remove_physical_filament(2); // removes pairs with component 2

    // Should remove 1+2 and 2+3, keep 1+3 (renumbered to 1+2)
    CHECK(mgr.filaments().size() == 1);
    CHECK(mgr.filaments()[0].component_a == 1);
    CHECK(mgr.filaments()[0].component_b == 2); // was 3, shifted down
}

// ---- clear_custom_entries ----

TEST_CASE("clear_custom_entries removes only custom rows", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#00FF00", "#0000FF"};
    mgr.auto_generate(colours);
    mgr.add_custom(1, 2, 25, colours);
    mgr.add_custom(2, 3, 75, colours);

    CHECK(mgr.filaments().size() == 5); // 3 auto + 2 custom
    mgr.clear_custom_entries();
    CHECK(mgr.filaments().size() == 3); // only auto remain
}

// ---- normalize_manual_pattern ----

TEST_CASE("normalize_manual_pattern valid patterns", "[VirtualFilamentManager]") {
    CHECK(VirtualFilamentManager::normalize_manual_pattern("12") == "12");
    CHECK(VirtualFilamentManager::normalize_manual_pattern("1122") == "1122");
    CHECK(VirtualFilamentManager::normalize_manual_pattern("AB") == "12");
    CHECK(VirtualFilamentManager::normalize_manual_pattern("aabb") == "1122");
    CHECK(VirtualFilamentManager::normalize_manual_pattern("1 2") == "12");
    CHECK(VirtualFilamentManager::normalize_manual_pattern("1/2/1") == "121");
    CHECK(VirtualFilamentManager::normalize_manual_pattern("123") == "123");
}

TEST_CASE("normalize_manual_pattern invalid patterns", "[VirtualFilamentManager]") {
    CHECK(VirtualFilamentManager::normalize_manual_pattern("xyz") == "");
    CHECK(VirtualFilamentManager::normalize_manual_pattern("0") == "");
    CHECK(VirtualFilamentManager::normalize_manual_pattern("") == "");
}

TEST_CASE("mix_percent_from_manual_pattern", "[VirtualFilamentManager]") {
    CHECK(VirtualFilamentManager::mix_percent_from_manual_pattern("12") == 50);
    CHECK(VirtualFilamentManager::mix_percent_from_manual_pattern("112") == 33);
    CHECK(VirtualFilamentManager::mix_percent_from_manual_pattern("1") == 0);
    CHECK(VirtualFilamentManager::mix_percent_from_manual_pattern("2") == 100);
    CHECK(VirtualFilamentManager::mix_percent_from_manual_pattern("1111") == 0);
    CHECK(VirtualFilamentManager::mix_percent_from_manual_pattern("2222") == 100);
}

// ---- display_colors ----

TEST_CASE("display_colors returns colors for enabled filaments only", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#0000FF"};
    mgr.auto_generate(colours);

    auto dc = mgr.display_colors();
    CHECK(dc.size() == 1);
    CHECK_FALSE(dc[0].empty());
    CHECK(dc[0][0] == '#');
    CHECK(dc[0].size() == 7);

    // Disabling reserves the slot (ID numbering stability), so display_colors()
    // still emits an entry. Deleting removes it.
    mgr.filaments()[0].enabled = false;
    dc = mgr.display_colors();
    CHECK(dc.size() == 1);

    mgr.filaments()[0].deleted = true;
    dc = mgr.display_colors();
    CHECK(dc.empty());
}

// ---- blend_color ----

TEST_CASE("blend_color produces valid hex", "[VirtualFilamentManager]") {
    std::string result = VirtualFilamentManager::blend_color("#FF0000", "#0000FF", 1, 1);
    CHECK(result.size() == 7);
    CHECK(result[0] == '#');
}

TEST_CASE("blend_color with ratio 1:0 returns first color", "[VirtualFilamentManager]") {
    std::string result = VirtualFilamentManager::blend_color("#FF0000", "#0000FF", 1, 0);
    CHECK(result == "#FF0000");
}

TEST_CASE("blend_color with ratio 0:1 returns second color", "[VirtualFilamentManager]") {
    std::string result = VirtualFilamentManager::blend_color("#FF0000", "#0000FF", 0, 1);
    CHECK(result == "#0000FF");
}

TEST_CASE("blend_color same colors returns approximately same color", "[VirtualFilamentManager]") {
    std::string result = VirtualFilamentManager::blend_color("#808080", "#808080", 1, 1);
    CHECK(result.size() == 7);
    CHECK(result[0] == '#');
    // Polynomial model may have small rounding differences (±3)
    unsigned int r = std::stoul(result.substr(1, 2), nullptr, 16);
    unsigned int g = std::stoul(result.substr(3, 2), nullptr, 16);
    unsigned int b = std::stoul(result.substr(5, 2), nullptr, 16);
    CHECK(std::abs(int(r) - 128) <= 5);
    CHECK(std::abs(int(g) - 128) <= 5);
    CHECK(std::abs(int(b) - 128) <= 5);
}

// ---- blend_color_multi ----

TEST_CASE("blend_color_multi with single color", "[VirtualFilamentManager]") {
    auto result = VirtualFilamentManager::blend_color_multi({{"#FF0000", 100}});
    CHECK(result == "#FF0000");
}

TEST_CASE("blend_color_multi with empty input", "[VirtualFilamentManager]") {
    auto result = VirtualFilamentManager::blend_color_multi({});
    CHECK(result == "#000000");
}

TEST_CASE("blend_color_multi with two equal weights", "[VirtualFilamentManager]") {
    auto result = VirtualFilamentManager::blend_color_multi({{"#FF0000", 50}, {"#0000FF", 50}});
    CHECK(result.size() == 7);
    CHECK(result[0] == '#');
    // Should be a blend, not pure red or blue
    CHECK(result != "#FF0000");
    CHECK(result != "#0000FF");
}

// ---- PrintConfig integration ----

TEST_CASE("PrintConfig contains virtual filament options", "[VirtualFilament][Config]") {
    PrintConfig config;
    CHECK(config.virtual_filaments_enabled.value == false);
    CHECK(config.virtual_filament_definitions.value.empty());
    CHECK(config.virtual_filament_advanced_dithering.value == false);
    CHECK(config.virtual_filament_gradient_mode.value == false);
    CHECK(config.virtual_filament_height_lower_bound.value == Approx(0.04));
    CHECK(config.virtual_filament_height_upper_bound.value == Approx(0.16));
    CHECK(config.virtual_filament_surface_offset_enabled.value == false);
    CHECK(config.virtual_filament_top_dither_enabled.value == false);
    CHECK(config.virtual_filament_top_dither_segment_mm.value == Approx(1.5));
    CHECK(config.virtual_filament_top_dither_layers.value == 1);
}

TEST_CASE("DynamicPrintConfig can set virtual filament options", "[VirtualFilament][Config]") {
    DynamicPrintConfig config;
    config.set_key_value("virtual_filaments_enabled", new ConfigOptionBool(true));
    config.set_key_value("virtual_filament_definitions", new ConfigOptionString("1,2,1,0,50"));

    CHECK(config.opt_bool("virtual_filaments_enabled") == true);
    CHECK(config.opt_string("virtual_filament_definitions") == "1,2,1,0,50");
}

// ---- parse_color_input ----

TEST_CASE("parse_color_input accepts #RRGGBB", "[VirtualFilament][Color]") {
    CHECK(VirtualFilamentManager::parse_color_input("#FF0000") == "#FF0000");
    CHECK(VirtualFilamentManager::parse_color_input("#abcdef") == "#ABCDEF");
    CHECK(VirtualFilamentManager::parse_color_input("  #123456  ") == "#123456");
}

TEST_CASE("parse_color_input expands #RGB to #RRGGBB", "[VirtualFilament][Color]") {
    CHECK(VirtualFilamentManager::parse_color_input("#F00") == "#FF0000");
    CHECK(VirtualFilamentManager::parse_color_input("#abc") == "#AABBCC");
}

TEST_CASE("parse_color_input accepts CSS named colors", "[VirtualFilament][Color]") {
    CHECK(VirtualFilamentManager::parse_color_input("red")     == "#FF0000");
    CHECK(VirtualFilamentManager::parse_color_input("BLUE")    == "#0000FF");
    CHECK(VirtualFilamentManager::parse_color_input("Orange")  == "#FFA500");
    CHECK(VirtualFilamentManager::parse_color_input("teal")    == "#008080");
    CHECK(VirtualFilamentManager::parse_color_input("magenta") == "#FF00FF");
}

TEST_CASE("parse_color_input rejects invalid input", "[VirtualFilament][Color]") {
    CHECK(VirtualFilamentManager::parse_color_input("")            == "");
    CHECK(VirtualFilamentManager::parse_color_input("#GGGGGG")     == "");
    CHECK(VirtualFilamentManager::parse_color_input("#12345")      == "");
    CHECK(VirtualFilamentManager::parse_color_input("not-a-color") == "");
}

// ---- pattern_from_ratios ----

TEST_CASE("pattern_from_ratios emits run-encoded string", "[VirtualFilament][Color]") {
    CHECK(VirtualFilamentManager::pattern_from_ratios({2, 3})       == "11222");
    CHECK(VirtualFilamentManager::pattern_from_ratios({1, 1, 1, 1}) == "1234");
    CHECK(VirtualFilamentManager::pattern_from_ratios({2, 0, 3})    == "11333");
    CHECK(VirtualFilamentManager::pattern_from_ratios({})           == "");
}

// ---- solve_target_color ----

TEST_CASE("solve_target_color returns zero distance when target is a physical", "[VirtualFilament][Color]") {
    std::vector<std::string> cmyk = {"#00FFFF", "#FF00FF", "#FFFF00", "#000000"};
    auto sol = VirtualFilamentManager::solve_target_color("#FFFF00", cmyk, 12);
    // Yellow is physical index 2 (1-based: 3). Expect all ratio on that slot.
    CHECK(sol.ratios[2] == 12);
    CHECK(sol.ratios[0] == 0);
    CHECK(sol.ratios[1] == 0);
    CHECK(sol.ratios[3] == 0);
    CHECK(sol.achieved_color == "#FFFF00");
    CHECK(sol.rgb_distance < 0.01f);
}

TEST_CASE("solve_target_color finds orange from CMYK via yellow+magenta", "[VirtualFilament][Color]") {
    std::vector<std::string> cmyk = {"#00FFFF", "#FF00FF", "#FFFF00", "#000000"};
    auto sol = VirtualFilamentManager::solve_target_color("#FFA500", cmyk, 12);
    // Orange = yellow-dominant with some magenta, no cyan/black.
    CHECK(sol.ratios[2] > 0);    // yellow present
    CHECK(sol.ratios[1] > 0);    // magenta present
    CHECK(sol.ratios[0] == 0);   // no cyan
    // Sum equals denominator.
    int sum = 0; for (int r : sol.ratios) sum += r;
    CHECK(sum == 12);
    // Pattern is non-empty and valid.
    CHECK(!sol.pattern.empty());
    CHECK(sol.pattern.size() == size_t(sum));
}

TEST_CASE("solve_target_color handles a single physical", "[VirtualFilament][Color]") {
    std::vector<std::string> one = {"#FF0000"};
    auto sol = VirtualFilamentManager::solve_target_color("#00FF00", one, 8);
    CHECK(sol.ratios.size() == 1);
    CHECK(sol.ratios[0] == 8);
}

TEST_CASE("solve_target_color handles empty palette", "[VirtualFilament][Color]") {
    auto sol = VirtualFilamentManager::solve_target_color("#FF0000", {}, 12);
    CHECK(sol.ratios.empty());
    CHECK(sol.pattern.empty());
}

// ---- add_custom_from_target_color ----

TEST_CASE("add_custom_from_target_color appends a Simple-mode pattern", "[VirtualFilamentManager][Color]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> cmyk = {"#00FFFF", "#FF00FF", "#FFFF00", "#000000"};
    mgr.auto_generate(cmyk);
    const size_t before = mgr.filaments().size();

    const int idx = mgr.add_custom_from_target_color("orange", cmyk, 12);
    REQUIRE(idx >= 0);
    CHECK(mgr.filaments().size() == before + 1);

    const auto &vf = mgr.filaments()[size_t(idx)];
    CHECK(vf.custom == true);
    CHECK(vf.enabled == true);
    CHECK(!vf.manual_pattern.empty());
    CHECK(vf.distribution_mode == int(VirtualFilament::Simple));
    CHECK(!vf.display_color.empty());
}

TEST_CASE("add_custom_from_target_color rejects invalid color", "[VirtualFilamentManager][Color]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> cmyk = {"#00FFFF", "#FF00FF", "#FFFF00", "#000000"};
    mgr.auto_generate(cmyk);

    CHECK(mgr.add_custom_from_target_color("", cmyk, 12) == -1);
    CHECK(mgr.add_custom_from_target_color("nonsense", cmyk, 12) == -1);
    CHECK(mgr.add_custom_from_target_color("#XYZ123", cmyk, 12) == -1);
}

TEST_CASE("add_custom_from_target_color display color survives round trip", "[VirtualFilamentManager][Color]") {
    // Regression: refresh_display_colors() used to re-derive the color from
    // component_a + component_b only, collapsing a 3+ component custom to a
    // 2-component blend of physicals 1 and 2.
    VirtualFilamentManager mgr;
    std::vector<std::string> palette = {"#00FFFF", "#FF00FF", "#FFFF00", "#000000"};
    mgr.auto_generate(palette);

    const int idx = mgr.add_custom_from_target_color("#FB6731", palette, 12);
    REQUIRE(idx >= 0);
    const std::string before = mgr.filaments()[size_t(idx)].display_color;
    REQUIRE(!before.empty());

    // Round-trip through serialize/deserialize.
    const std::string serialized = mgr.serialize();
    VirtualFilamentManager restored;
    restored.auto_generate(palette);
    restored.deserialize(serialized, palette);

    // Find the custom entry in the restored manager. auto_generate + custom
    // order is preserved by the manager; walk for the matching pattern.
    int restored_idx = -1;
    for (size_t i = 0; i < restored.filaments().size(); ++i)
        if (restored.filaments()[i].custom) { restored_idx = int(i); break; }
    REQUIRE(restored_idx >= 0);

    // Color must match. This would fail if refresh_display_colors collapsed
    // it back to a 2-component cyan+magenta blend.
    CHECK(restored.filaments()[size_t(restored_idx)].display_color == before);
}

TEST_CASE("add_custom_from_target_color uses all non-zero physicals in resolution", "[VirtualFilamentManager][Color]") {
    // Regression: previous versions set component_a/_b to the first two
    // non-zero physicals, which caused pattern tokens '1' and '2' to alias
    // onto those two and drop every other physical from the mix.
    VirtualFilamentManager mgr;
    std::vector<std::string> palette = {"#00FFFF", "#FF00FF", "#FFFF00", "#000000"};
    mgr.auto_generate(palette);

    const int idx = mgr.add_custom_from_target_color("#FB6731", palette, 12);
    REQUIRE(idx >= 0);

    const size_t num_physical = palette.size();
    const auto &vf = mgr.filaments()[size_t(idx)];
    REQUIRE(!vf.manual_pattern.empty());

    // Find the filament_id this virtual resolves through.
    size_t enabled_pos = 0;
    for (int i = 0; i <= idx; ++i)
        if (mgr.filaments()[size_t(i)].enabled && !mgr.filaments()[size_t(i)].deleted)
            ++enabled_pos;
    const unsigned int vid = unsigned(num_physical + enabled_pos);

    // Resolve every layer in the cycle; count how many layers each physical
    // actually receives. Every non-zero ratio must be reflected in the walk.
    std::vector<int> resolved_by_physical(num_physical + 1, 0);
    const size_t pattern_len = vf.manual_pattern.size();
    for (size_t layer = 0; layer < pattern_len; ++layer) {
        const unsigned int phys = mgr.resolve(vid, num_physical, int(layer));
        REQUIRE(phys >= 1);
        REQUIRE(phys <= num_physical);
        ++resolved_by_physical[phys];
    }
    // Spot-check: at least 3 distinct physicals should be hit for this
    // target (CMYK blend of a warm orange-red uses M+Y plus a touch of K).
    int distinct = 0;
    for (size_t p = 1; p <= num_physical; ++p)
        if (resolved_by_physical[p] > 0) ++distinct;
    CHECK(distinct >= 3);

    // And the per-pattern resolve counts must match pattern_from_ratios's
    // runs-encoding of the stored ratios (i.e. no physical silently dropped).
    // Walk the raw pattern and count tokens 1..num_physical.
    std::vector<int> token_counts(num_physical + 1, 0);
    for (char c : vf.manual_pattern) {
        if (c < '1' || c > '9') continue;
        unsigned int tok = unsigned(c - '0');
        if (tok >= 1 && tok <= num_physical) ++token_counts[tok];
    }
    for (size_t p = 1; p <= num_physical; ++p)
        CHECK(resolved_by_physical[p] == token_counts[p]);
}

TEST_CASE("add_custom_from_target_color resolves per-layer through manual_pattern", "[VirtualFilamentManager][Color]") {
    VirtualFilamentManager mgr;
    // Use a target that's clearly mid-way between two physicals so the solver
    // must blend. Start with red+blue physicals; target a purple that neither
    // one alone matches well.
    std::vector<std::string> palette = {"#FF0000", "#0000FF", "#FFFFFF", "#000000"};
    mgr.auto_generate(palette);

    const int idx = mgr.add_custom_from_target_color("#800080", palette, 12);
    REQUIRE(idx >= 0);

    const size_t num_physical = palette.size();
    const auto &vf = mgr.filaments()[size_t(idx)];

    // Find the filament_id this virtual resolves through. It's num_physical +
    // 1-based enabled-index. Count enabled virtuals up to (and including) idx.
    size_t enabled_pos = 0;
    for (int i = 0; i <= idx; ++i)
        if (mgr.filaments()[size_t(i)].enabled && !mgr.filaments()[size_t(i)].deleted)
            ++enabled_pos;
    const unsigned int vid = unsigned(num_physical + enabled_pos);

    // Walk the pattern and check each layer resolves to a valid physical.
    const size_t pattern_len = vf.manual_pattern.size();
    REQUIRE(pattern_len > 0);
    for (size_t layer = 0; layer < pattern_len; ++layer) {
        const unsigned int phys = mgr.resolve(vid, num_physical, int(layer));
        CHECK(phys >= 1);
        CHECK(phys <= num_physical);
    }
}

// ---- name + update ------------------------------------------------

TEST_CASE("name round-trips through serialize/deserialize", "[VirtualFilamentManager]") {
    const std::vector<std::string> palette = {
        "#21FFFF", "#FB02FF", "#FFFF0A", "#000000"
    };
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);

    // Add one custom with a tricky name (spaces, comma, semicolon, unicode).
    const int idx = mgr.add_custom_from_target_color(
        "#CC7733", palette, 12, "Brand, Orange; 2024 \xc3\xa9");
    REQUIRE(idx >= 0);

    // Also name an auto row.
    mgr.filaments()[0].name = "My Teal";

    const std::string serialized = mgr.serialize();

    VirtualFilamentManager mgr2;
    mgr2.auto_generate(palette);
    mgr2.deserialize(serialized, palette);

    REQUIRE(mgr2.filaments().size() == mgr.filaments().size());
    CHECK(mgr2.filaments()[0].name == std::string("My Teal"));
    CHECK(mgr2.filaments()[size_t(idx)].name ==
          std::string("Brand, Orange; 2024 \xc3\xa9"));
}

TEST_CASE("update_from_target_color edits an existing row", "[VirtualFilamentManager][Color]") {
    const std::vector<std::string> palette = {
        "#21FFFF", "#FB02FF", "#FFFF0A", "#000000"
    };
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);

    const int idx = mgr.add_custom_from_target_color("#FFA500", palette, 12);
    REQUIRE(idx >= 0);
    const std::string before = mgr.filaments()[size_t(idx)].display_color;

    REQUIRE(mgr.update_from_target_color(size_t(idx), "#800080",
                                         "Royal Purple", palette, 12));
    const auto &vf = mgr.filaments()[size_t(idx)];
    CHECK(vf.name == "Royal Purple");
    CHECK(vf.custom);
    CHECK_FALSE(vf.origin_auto);
    CHECK(vf.display_color != before);
    CHECK_FALSE(vf.manual_pattern.empty());
}

TEST_CASE("update_from_target_color converts auto row to custom", "[VirtualFilamentManager][Color]") {
    const std::vector<std::string> palette = {
        "#21FFFF", "#FB02FF", "#FFFF0A"
    };
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    REQUIRE(mgr.filaments().size() >= 1);
    REQUIRE_FALSE(mgr.filaments()[0].custom);  // auto row

    REQUIRE(mgr.update_from_target_color(0, "#008080", "Teal", palette, 12));
    const auto &vf = mgr.filaments()[0];
    CHECK(vf.custom);
    CHECK_FALSE(vf.origin_auto);
    CHECK(vf.name == "Teal");
    CHECK_FALSE(vf.manual_pattern.empty());
}

TEST_CASE("update_from_target_color rejects bad input", "[VirtualFilamentManager][Color]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);

    CHECK_FALSE(mgr.update_from_target_color(0, "", "x", palette, 12));
    CHECK_FALSE(mgr.update_from_target_color(0, "nonsense", "x", palette, 12));
    CHECK_FALSE(mgr.update_from_target_color(99, "#FF00FF", "x", palette, 12));
}

// Regression: after editing an auto-generated row into a custom one, a
// serialize -> auto_generate -> deserialize round-trip must not resurrect the
// original auto pair. (Previously, the deserializer's auto-append fallback
// re-created the (1,2) auto row because `consumed_pairs` only tracked
// non-custom rows from the serialized stream.)
TEST_CASE("edited auto row is not duplicated after rebuild", "[VirtualFilamentManager][Serialize]") {
    const std::vector<std::string> palette = {"#21FFFF", "#FB02FF", "#FFFF0A"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    REQUIRE(mgr.filaments().size() >= 1);
    REQUIRE_FALSE(mgr.filaments()[0].custom);

    // Edit the first auto row (canonical pair (1,2)) into a custom one.
    REQUIRE(mgr.update_from_target_color(0, "#008080", "Teal", palette, 12));
    const size_t reserved_before = mgr.reserved_count();
    REQUIRE(mgr.filaments()[0].custom);
    REQUIRE(mgr.filaments()[0].component_a == 1);
    REQUIRE(mgr.filaments()[0].component_b == 2);

    const std::string serialized = mgr.serialize();

    // Simulate a project reload: start from scratch, auto_generate, then apply
    // the previously serialized definitions on top.
    VirtualFilamentManager rebuilt;
    rebuilt.auto_generate(palette);
    rebuilt.deserialize(serialized, palette);

    CHECK(rebuilt.reserved_count() == reserved_before);

    // Exactly one row should own the canonical (1,2) pair, and it must be the
    // edited custom row — not a resurrected auto row.
    size_t count_1_2 = 0;
    bool found_custom_teal = false;
    for (const auto &vf : rebuilt.filaments()) {
        if (vf.deleted) continue;
        const unsigned int lo = std::min(vf.component_a, vf.component_b);
        const unsigned int hi = std::max(vf.component_a, vf.component_b);
        if (lo == 1 && hi == 2) {
            ++count_1_2;
            if (vf.custom && vf.name == "Teal")
                found_custom_teal = true;
        }
    }
    CHECK(count_1_2 == 1);
    CHECK(found_custom_teal);
}

// ---- Phase 6 extensions ----

TEST_CASE("height-weighted cadence resolves by Z phase", "[VirtualFilamentManager][Gradient]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    REQUIRE(mgr.filaments().size() == 1);

    // Force a custom row at mix_b = 25% with no manual_pattern so the
    // height-weighted resolve path is the one actually exercised.
    auto &vf = mgr.filaments()[0];
    vf.custom = true;
    vf.origin_auto = false;
    vf.mix_b_percent = 25;
    vf.manual_pattern.clear();

    // Gradient mode ON, lo=0.1, hi=0.3: mix_b=25% -> pct_a=0.75 ->
    // h_a = 0.1 + 0.75*0.2 = 0.25, h_b = 0.1 + 0.25*0.2 = 0.15, cycle_h = 0.4.
    mgr.apply_gradient_settings(1, 0.1f, 0.3f, false);

    const size_t num_physical = palette.size();
    const unsigned int vid = unsigned(num_physical) + 1;
    const float lh = 0.2f;

    // z_anchor = 0.2 - 0.1 = 0.1; phase = 0.1 < h_a (0.25) -> A.
    CHECK(mgr.resolve(vid, num_physical, 0, 0.2f, lh) == 1u);
    // z_anchor = 0.4 - 0.1 = 0.3; phase = 0.3 % 0.4 = 0.3 >= h_a -> B.
    CHECK(mgr.resolve(vid, num_physical, 1, 0.4f, lh) == 2u);
}

TEST_CASE("gradient mode falls back to simple without layer height", "[VirtualFilamentManager][Gradient]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    auto &vf = mgr.filaments()[0];
    vf.custom = true;
    vf.mix_b_percent = 25;
    vf.manual_pattern.clear();
    mgr.apply_gradient_settings(1, 0.1f, 0.3f, false);

    const size_t num_physical = palette.size();
    const unsigned int vid = unsigned(num_physical) + 1;
    // layer_height == 0 disables the height-weighted branch and falls
    // through to layer-cycle cadence; resolve() must still return a
    // physical id.
    const unsigned int r = mgr.resolve(vid, num_physical, 3, 0.f, 0.f);
    CHECK((r == 1u || r == 2u));
}

TEST_CASE("local_z_max_sublayers caps consecutive runs", "[VirtualFilamentManager][ZCap]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    REQUIRE(mgr.filaments().size() == 1);
    auto &vf = mgr.filaments()[0];
    // Force an asymmetric run pattern that would exceed a cap of 2.
    vf.ratio_a = 5;
    vf.ratio_b = 2;
    vf.manual_pattern.clear();
    vf.local_z_max_sublayers = 2;
    vf.custom = true;

    const size_t num_physical = palette.size();
    const unsigned int vid = unsigned(num_physical) + 1;
    // Sample across multiple cycles so that any wraparound run is caught.
    const int sample = 24;
    std::vector<unsigned int> seq;
    seq.reserve(sample);
    for (int i = 0; i < sample; ++i)
        seq.push_back(mgr.resolve(vid, num_physical, i, 0.f, 0.f));

    int run = 1;
    int max_run = 1;
    for (size_t i = 1; i < seq.size(); ++i) {
        if (seq[i] == seq[i - 1]) ++run;
        else run = 1;
        max_run = std::max(max_run, run);
    }
    CHECK(max_run <= 2);
    // 5:2 with cap 2 is wraparound-infeasible (5 A's cannot split into 2
    // groups of <=2 that also keep the cycle boundary safe), so totals are
    // best-effort. Confirm the minority still appears and the majority is
    // represented.
    const int count_a = int(std::count(seq.begin(), seq.end(), 1u));
    const int count_b = int(std::count(seq.begin(), seq.end(), 2u));
    CHECK(count_a > 0);
    CHECK(count_b > 0);
    CHECK(count_a > count_b);
}

TEST_CASE("local_z_max_sublayers honors cap when one side is exhausted",
          "[VirtualFilamentManager][ZCap]") {
    // Regression: with ratio 5:1 and cap 2 the tail of the cycle previously
    // emitted a run of 3 A's because the "rem_b == 0" branch skipped the cap
    // check. The fix truncates the cycle so the cap is always honored.
    const std::vector<std::string> palette = {"#FF0000", "#00FF00"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    REQUIRE(mgr.filaments().size() == 1);
    auto &vf = mgr.filaments()[0];
    vf.ratio_a = 5;
    vf.ratio_b = 1;
    vf.manual_pattern.clear();
    vf.local_z_max_sublayers = 2;
    vf.custom = true;

    const size_t num_physical = palette.size();
    const unsigned int vid = unsigned(num_physical) + 1;
    // Sample enough layers that any cycle wraparound is exercised.
    const int sample = 24;
    std::vector<unsigned int> seq;
    seq.reserve(sample);
    for (int i = 0; i < sample; ++i)
        seq.push_back(mgr.resolve(vid, num_physical, i, 0.f, 0.f));

    int run = 1;
    int max_run = 1;
    for (size_t i = 1; i < seq.size(); ++i) {
        if (seq[i] == seq[i - 1]) ++run;
        else run = 1;
        max_run = std::max(max_run, run);
    }
    CHECK(max_run <= 2);
}

TEST_CASE("local_z_max_sublayers serializes round-trip", "[VirtualFilamentManager][ZCap][Serialize]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00", "#0000FF"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    REQUIRE(mgr.filaments().size() >= 1);
    mgr.filaments()[0].local_z_max_sublayers = 4;

    const std::string wire = mgr.serialize();
    CHECK(wire.find(",z4") != std::string::npos);

    VirtualFilamentManager rebuilt;
    rebuilt.auto_generate(palette);
    rebuilt.deserialize(wire, palette);
    REQUIRE(rebuilt.filaments().size() >= 1);
    CHECK(rebuilt.filaments()[0].local_z_max_sublayers == 4);
}

TEST_CASE("gradient_component_ids drives 3+ component resolve", "[VirtualFilamentManager][MultiGradient]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00", "#0000FF", "#000000"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    REQUIRE(mgr.filaments().size() >= 1);
    auto &vf = mgr.filaments()[0];
    vf.manual_pattern.clear();
    // Three-component gradient: ids {1, 2, 3} with equal weights.
    vf.gradient_component_ids    = {1u, 2u, 3u};
    vf.gradient_component_weights = {1, 1, 1};

    const size_t num_physical = palette.size();
    const unsigned int vid = unsigned(num_physical) + 1;

    // Resolve 3 consecutive layers: expect the full set {1, 2, 3}
    // (balanced sequence hits each id exactly once per cycle).
    std::set<unsigned int> seen;
    for (int i = 0; i < 3; ++i)
        seen.insert(mgr.resolve(vid, num_physical, i, 0.f, 0.f));
    CHECK(seen.size() == 3);
    CHECK(seen.count(1u) == 1);
    CHECK(seen.count(2u) == 1);
    CHECK(seen.count(3u) == 1);
}

TEST_CASE("gradient_component_ids honors weights", "[VirtualFilamentManager][MultiGradient]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00", "#0000FF"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    auto &vf = mgr.filaments()[0];
    vf.manual_pattern.clear();
    vf.gradient_component_ids     = {1u, 2u, 3u};
    vf.gradient_component_weights = {2, 1, 1}; // expect 1 twice per cycle of 4

    const size_t num_physical = palette.size();
    const unsigned int vid = unsigned(num_physical) + 1;
    std::map<unsigned int, int> counts;
    for (int i = 0; i < 4; ++i)
        ++counts[mgr.resolve(vid, num_physical, i, 0.f, 0.f)];
    CHECK(counts[1u] == 2);
    CHECK(counts[2u] == 1);
    CHECK(counts[3u] == 1);
}

TEST_CASE("gradient_component_ids serializes round-trip", "[VirtualFilamentManager][MultiGradient][Serialize]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00", "#0000FF"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    auto &vf = mgr.filaments()[0];
    vf.gradient_component_ids     = {1u, 2u, 3u};
    vf.gradient_component_weights = {3, 1, 2};

    const std::string wire = mgr.serialize();
    CHECK(wire.find(",g1|2|3") != std::string::npos);
    CHECK(wire.find(",w3|1|2") != std::string::npos);

    VirtualFilamentManager rebuilt;
    rebuilt.auto_generate(palette);
    rebuilt.deserialize(wire, palette);
    REQUIRE(rebuilt.filaments().size() >= 1);
    const auto &gv = rebuilt.filaments()[0];
    CHECK(gv.gradient_component_ids == std::vector<unsigned int>{1u, 2u, 3u});
    CHECK(gv.gradient_component_weights == std::vector<int>{3, 1, 2});
}

TEST_CASE("gradient_component_ids drops stale physical ids on reload", "[VirtualFilamentManager][MultiGradient][Serialize]") {
    // Wire contains a reference to physical id 4, but reload palette only has 3.
    const std::string wire = "1,2,1,1,50,m2,d0,o0,u1001,g1|2|4,w1|1|1";
    const std::vector<std::string> palette = {"#FF0000", "#00FF00", "#0000FF"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    mgr.deserialize(wire, palette);
    // Row survives, but gradient collapses (only 2 valid ids after filter).
    REQUIRE(mgr.filaments().size() >= 1);
    CHECK(mgr.filaments()[0].gradient_component_ids.empty());
    CHECK(mgr.filaments()[0].gradient_component_weights.empty());
}

// ---- Surface-bias offsets (Phase 6e data layer) ----

TEST_CASE("component_surface_offset returns 0 for non-virtual IDs",
          "[VirtualFilamentManager][SurfaceBias]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    // Physical ID 1 is not virtual.
    CHECK(mgr.component_surface_offset(1, palette.size(), 0, 0.f, 0.f) == 0.f);
    // ID beyond any virtual slot returns 0.
    CHECK(mgr.component_surface_offset(99, palette.size(), 0, 0.f, 0.f) == 0.f);
}

TEST_CASE("component_surface_offset returns 0 when no bias is configured",
          "[VirtualFilamentManager][SurfaceBias]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    REQUIRE(mgr.filaments().size() == 1);
    const size_t num_physical = palette.size();
    const unsigned int vid = unsigned(num_physical) + 1;
    for (int i = 0; i < 4; ++i)
        CHECK(mgr.component_surface_offset(vid, num_physical, i, 0.f, 0.f) == 0.f);
}

TEST_CASE("component_surface_offset applies B-side bias only on B layers",
          "[VirtualFilamentManager][SurfaceBias]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    REQUIRE(mgr.filaments().size() == 1);
    auto &vf = mgr.filaments()[0];
    vf.ratio_a = 1;
    vf.ratio_b = 1;
    vf.manual_pattern.clear();
    vf.component_b_surface_offset = 0.2f; // positive -> B outward

    const size_t num_physical = palette.size();
    const unsigned int vid = unsigned(num_physical) + 1;

    // With ratio 1:1, layer_index even -> A, odd -> B.
    const unsigned int r0 = mgr.resolve(vid, num_physical, 0, 0.f, 0.f);
    const unsigned int r1 = mgr.resolve(vid, num_physical, 1, 0.f, 0.f);
    const float o0 = mgr.component_surface_offset(vid, num_physical, 0, 0.f, 0.f);
    const float o1 = mgr.component_surface_offset(vid, num_physical, 1, 0.f, 0.f);

    // Whichever layer picks B gets the +0.2 offset; the A layer stays at 0.
    if (r0 == vf.component_b) { CHECK(o0 == Approx(0.2f)); CHECK(o1 == 0.f); }
    else                      { CHECK(o1 == Approx(0.2f)); CHECK(o0 == 0.f); }
    CHECK(r0 != r1); // sanity: 1:1 alternates
}

TEST_CASE("component_surface_offset applies A-side bias as negative sign",
          "[VirtualFilamentManager][SurfaceBias]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    auto &vf = mgr.filaments()[0];
    vf.ratio_a = 1;
    vf.ratio_b = 1;
    vf.manual_pattern.clear();
    vf.component_a_surface_offset = 0.15f; // A outward -> negative signed_bias

    const size_t num_physical = palette.size();
    const unsigned int vid = unsigned(num_physical) + 1;
    // Scan a full cycle: whichever side resolves to A should get +0.15.
    int a_offsets = 0, b_offsets = 0;
    for (int i = 0; i < 4; ++i) {
        const unsigned int r = mgr.resolve(vid, num_physical, i, 0.f, 0.f);
        const float o = mgr.component_surface_offset(vid, num_physical, i, 0.f, 0.f);
        if (r == vf.component_a) { CHECK(o == Approx(0.15f)); ++a_offsets; }
        else                     { CHECK(o == 0.f); ++b_offsets; }
    }
    CHECK(a_offsets > 0);
    CHECK(b_offsets > 0);
}

TEST_CASE("component_surface_offset larger magnitude wins when both set",
          "[VirtualFilamentManager][SurfaceBias]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    auto &vf = mgr.filaments()[0];
    vf.ratio_a = 1;
    vf.ratio_b = 1;
    vf.manual_pattern.clear();
    vf.component_a_surface_offset = 0.1f;
    vf.component_b_surface_offset = 0.3f; // B wins

    const size_t num_physical = palette.size();
    const unsigned int vid = unsigned(num_physical) + 1;
    // B-biased: only B layers get 0.3; A layers get 0 (A's 0.1 is overridden).
    for (int i = 0; i < 4; ++i) {
        const unsigned int r = mgr.resolve(vid, num_physical, i, 0.f, 0.f);
        const float o = mgr.component_surface_offset(vid, num_physical, i, 0.f, 0.f);
        if (r == vf.component_b) CHECK(o == Approx(0.3f));
        else                     CHECK(o == 0.f);
    }
}

TEST_CASE("component_surface_offset returns 0 for multi-component manual patterns",
          "[VirtualFilamentManager][SurfaceBias]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00", "#0000FF"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    REQUIRE(mgr.filaments().size() >= 1);
    auto &vf = mgr.filaments()[0];
    vf.component_b_surface_offset = 0.3f;

    // Manual pattern referencing a third physical filament ('3') — bias must not apply,
    // since the two-color surface-bias model is undefined for 3+ components.
    vf.manual_pattern = "123";

    const size_t num_physical = palette.size();
    const unsigned int vid = unsigned(num_physical) + 1;
    for (int i = 0; i < 6; ++i)
        CHECK(mgr.component_surface_offset(vid, num_physical, i, 0.f, 0.f) == 0.f);

    // Same guard via gradient_component_ids (3+ components).
    vf.manual_pattern.clear();
    vf.gradient_component_ids = {1, 2, 3};
    vf.gradient_component_weights = {1, 1, 1};
    for (int i = 0; i < 6; ++i)
        CHECK(mgr.component_surface_offset(vid, num_physical, i, 0.f, 0.f) == 0.f);
}

TEST_CASE("max_component_surface_offset reports largest enabled magnitude",
          "[VirtualFilamentManager][SurfaceBias]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00", "#0000FF"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    REQUIRE(mgr.filaments().size() >= 2);
    mgr.filaments()[0].component_b_surface_offset = 0.2f;
    mgr.filaments()[1].component_a_surface_offset = 0.5f;
    CHECK(mgr.max_component_surface_offset() == Approx(0.5f));

    // Disabled rows don't contribute.
    mgr.filaments()[1].enabled = false;
    CHECK(mgr.max_component_surface_offset() == Approx(0.2f));

    // Deleted rows don't contribute either.
    mgr.filaments()[0].deleted = true;
    CHECK(mgr.max_component_surface_offset() == 0.f);
}

TEST_CASE("surface offsets serialize round-trip",
          "[VirtualFilamentManager][SurfaceBias][Serialize]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    REQUIRE(mgr.filaments().size() >= 1);
    mgr.filaments()[0].component_a_surface_offset = 0.05f;
    mgr.filaments()[0].component_b_surface_offset = 0.25f;

    const std::string wire = mgr.serialize();
    CHECK(wire.find(",sa") != std::string::npos);
    CHECK(wire.find(",sb") != std::string::npos);

    VirtualFilamentManager rebuilt;
    rebuilt.auto_generate(palette);
    rebuilt.deserialize(wire, palette);
    REQUIRE(rebuilt.filaments().size() >= 1);
    CHECK(rebuilt.filaments()[0].component_a_surface_offset == Approx(0.05f));
    CHECK(rebuilt.filaments()[0].component_b_surface_offset == Approx(0.25f));
}

TEST_CASE("surface offsets omitted from wire when zero",
          "[VirtualFilamentManager][SurfaceBias][Serialize]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    const std::string wire = mgr.serialize();
    CHECK(wire.find(",sa") == std::string::npos);
    CHECK(wire.find(",sb") == std::string::npos);
}

// ---------------------------------------------------------------------------
// resolve_segment — top-surface dithering
// ---------------------------------------------------------------------------

TEST_CASE("resolve_segment returns filament_id unchanged for non-virtual",
          "[VirtualFilamentManager][TopDither]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    CHECK(mgr.resolve_segment(1, palette.size(), 0, 0) == 1u);
    CHECK(mgr.resolve_segment(2, palette.size(), 5, 7) == 2u);
    CHECK(mgr.resolve_segment(99, palette.size(), 0, 0) == 99u);
}

TEST_CASE("resolve_segment alternates across segments on a 1:1 virtual",
          "[VirtualFilamentManager][TopDither]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    REQUIRE(mgr.filaments().size() >= 1);
    auto &vf = mgr.filaments()[0];
    vf.ratio_a = 1; vf.ratio_b = 1;
    vf.manual_pattern.clear();

    const size_t num_physical = palette.size();
    const unsigned int vid = unsigned(num_physical) + 1;

    // At layer 0, segments 0,1,2,3,... alternate A,B,A,B.
    CHECK(mgr.resolve_segment(vid, num_physical, 0, 0) == vf.component_a);
    CHECK(mgr.resolve_segment(vid, num_physical, 0, 1) == vf.component_b);
    CHECK(mgr.resolve_segment(vid, num_physical, 0, 2) == vf.component_a);
    CHECK(mgr.resolve_segment(vid, num_physical, 0, 3) == vf.component_b);

    // Layer phase shift: at layer 1, the starting component flips.
    CHECK(mgr.resolve_segment(vid, num_physical, 1, 0) == vf.component_b);
    CHECK(mgr.resolve_segment(vid, num_physical, 1, 1) == vf.component_a);
}

TEST_CASE("resolve_segment honors 2:1 ratio across segments",
          "[VirtualFilamentManager][TopDither]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    auto &vf = mgr.filaments()[0];
    vf.ratio_a = 2; vf.ratio_b = 1;
    vf.manual_pattern.clear();

    const size_t num_physical = palette.size();
    const unsigned int vid = unsigned(num_physical) + 1;

    // Count A vs B over a large window; should approximate 2:1.
    int count_a = 0, count_b = 0;
    for (int s = 0; s < 60; ++s) {
        unsigned int r = mgr.resolve_segment(vid, num_physical, 0, s);
        if (r == vf.component_a) ++count_a;
        else if (r == vf.component_b) ++count_b;
    }
    CHECK(count_a == 40);
    CHECK(count_b == 20);
}

TEST_CASE("resolve_segment respects run-length cap within a single layer",
          "[VirtualFilamentManager][TopDither]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    auto &vf = mgr.filaments()[0];
    vf.ratio_a = 5; vf.ratio_b = 1;
    vf.local_z_max_sublayers = 2; // enable cap
    vf.manual_pattern.clear();

    const size_t num_physical = palette.size();
    const unsigned int vid = unsigned(num_physical) + 1;

    // Scan segment sequence; longest consecutive run of A must be <= 2.
    int run = 0, max_run = 0;
    for (int s = 0; s < 30; ++s) {
        unsigned int r = mgr.resolve_segment(vid, num_physical, 0, s);
        if (r == vf.component_a) { ++run; max_run = std::max(max_run, run); }
        else run = 0;
    }
    CHECK(max_run <= vf.local_z_max_sublayers);
}

TEST_CASE("resolve_segment handles manual pattern",
          "[VirtualFilamentManager][TopDither]") {
    const std::vector<std::string> palette = {"#FF0000", "#00FF00"};
    VirtualFilamentManager mgr;
    mgr.auto_generate(palette);
    auto &vf = mgr.filaments()[0];
    vf.manual_pattern = "112"; // A,A,B cycle

    const size_t num_physical = palette.size();
    const unsigned int vid = unsigned(num_physical) + 1;

    // At layer 0, segments 0,1,2 → A,A,B; then repeats.
    CHECK(mgr.resolve_segment(vid, num_physical, 0, 0) == vf.component_a);
    CHECK(mgr.resolve_segment(vid, num_physical, 0, 1) == vf.component_a);
    CHECK(mgr.resolve_segment(vid, num_physical, 0, 2) == vf.component_b);
    CHECK(mgr.resolve_segment(vid, num_physical, 0, 3) == vf.component_a);
}

// ---- Advanced dithering ----------------------------------------------------
// Reaches use_component_b_advanced_dither() through the public resolve()
// path. The dither's promise is that, over a window of K full cycles, the
// proportion of B layers stays within ±1 of the configured ratio_b/cycle —
// i.e. it's a phase-shifted Bresenham over an arbitrary number of layers.

// Helper: configure a single virtual filament with custom ratios + advanced
// dithering enabled. apply_gradient_settings recomputes ratios for custom
// rows, so it must run *before* the manual override.
static void setup_dithered(VirtualFilamentManager &mgr, int ratio_a, int ratio_b)
{
    std::vector<std::string> colours = {"#FF0000", "#0000FF"};
    mgr.auto_generate(colours);
    mgr.filaments()[0].custom = true;
    mgr.apply_gradient_settings(0 /*gradient_mode*/, 0.f, 0.f, /*advanced_dithering=*/true);
    mgr.filaments()[0].ratio_a = ratio_a;
    mgr.filaments()[0].ratio_b = ratio_b;
}

TEST_CASE("advanced dithering preserves overall ratio over many layers",
          "[VirtualFilamentManager][AdvancedDither]") {
    VirtualFilamentManager mgr;
    setup_dithered(mgr, 3, 2);  // 60% A, 40% B
    const auto &vf = mgr.filaments()[0];

    const int N = 5000;
    int count_b = 0;
    for (int i = 0; i < N; ++i)
        if (mgr.resolve(3, 2, i) == vf.component_b) ++count_b;
    // Within 1% of the ideal 40% over 5000 layers
    CHECK(double(count_b) / double(N) == Approx(0.4).margin(0.01));
}

TEST_CASE("advanced dithering with prime cycle length stays balanced",
          "[VirtualFilamentManager][AdvancedDither]") {
    // 7:13 — exercises the gcd-step finder in dithering_phase_step().
    // Bresenham invariant: over an exact multiple of the cycle (20), the B
    // count must be exactly ratio_b * (N / cycle) = 13 * 200 = 2600.
    VirtualFilamentManager mgr;
    setup_dithered(mgr, 7, 13);
    const auto &vf = mgr.filaments()[0];

    const int N = 4000;  // 200 full cycles of length 20
    int count_b = 0;
    for (int i = 0; i < N; ++i)
        if (mgr.resolve(3, 2, i) == vf.component_b) ++count_b;
    CHECK(count_b == 2600);
}

TEST_CASE("advanced dithering tolerates very large layer indices",
          "[VirtualFilamentManager][AdvancedDither]") {
    // Internal cycle_idx * dithering_phase_step() uses int64_t to avoid
    // overflow. Drive the layer index high enough that a 32-bit multiply
    // would have wrapped, then check that resolve() still returns a
    // valid physical id (1 or 2) and not 0/garbage.
    VirtualFilamentManager mgr;
    setup_dithered(mgr, 11, 13);
    const auto &vf = mgr.filaments()[0];

    for (int layer : { 1'000'000, 100'000'000, 2'000'000'000 }) {
        const unsigned int r = mgr.resolve(3, 2, layer);
        CHECK((r == vf.component_a || r == vf.component_b));
    }
}

// ---- 3+ component gradient resolve ----------------------------------------
// Exercises build_weighted_gradient_sequence() through resolve(). The
// algorithm is greedy error-diffusion ("most behind its share wins"), with
// a hard cap on the cycle length (k_max_cycle = 48).

TEST_CASE("3-component gradient with equal weights distributes evenly",
          "[VirtualFilamentManager][Gradient]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#00FF00", "#0000FF"};
    mgr.auto_generate(colours);
    auto &vf = mgr.filaments()[0];
    vf.custom = true;
    vf.gradient_component_ids     = { 1, 2, 3 };
    vf.gradient_component_weights = { 1, 1, 1 };

    const int N = 300;     // multiple of 3
    std::map<unsigned int, int> hits;
    for (int i = 0; i < N; ++i)
        ++hits[mgr.resolve(4 /*virtual id*/, 3 /*num_physical*/, i)];

    CHECK(hits[1] == N / 3);
    CHECK(hits[2] == N / 3);
    CHECK(hits[3] == N / 3);
}

TEST_CASE("3-component gradient with weighted ratio honors proportions",
          "[VirtualFilamentManager][Gradient]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#00FF00", "#0000FF"};
    mgr.auto_generate(colours);
    auto &vf = mgr.filaments()[0];
    vf.custom = true;
    vf.gradient_component_ids     = { 1, 2, 3 };
    vf.gradient_component_weights = { 4, 2, 1 };  // sum=7 → cycle=7 (no GCD reduction)

    std::map<unsigned int, int> hits;
    for (int i = 0; i < 7; ++i) ++hits[mgr.resolve(4, 3, i)];
    CHECK(hits[1] == 4);
    CHECK(hits[2] == 2);
    CHECK(hits[3] == 1);
}

TEST_CASE("3-component gradient applies GCD reduction to weights",
          "[VirtualFilamentManager][Gradient]") {
    // 4:6:8 has gcd 2 → reduced to 2:3:4 → cycle of 9 (not 18).
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#00FF00", "#0000FF"};
    mgr.auto_generate(colours);
    auto &vf = mgr.filaments()[0];
    vf.custom = true;
    vf.gradient_component_ids     = { 1, 2, 3 };
    vf.gradient_component_weights = { 4, 6, 8 };

    // The resolve at layer 0 and layer 9 must produce the same id (cycle = 9).
    const unsigned int r0 = mgr.resolve(4, 3, 0);
    const unsigned int r9 = mgr.resolve(4, 3, 9);
    CHECK(r0 == r9);

    // 9 layers must contain exactly the reduced counts.
    std::map<unsigned int, int> hits;
    for (int i = 0; i < 9; ++i) ++hits[mgr.resolve(4, 3, i)];
    CHECK(hits[1] == 2);
    CHECK(hits[2] == 3);
    CHECK(hits[3] == 4);
}

TEST_CASE("3-component gradient drops zero-weighted components",
          "[VirtualFilamentManager][Gradient]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#00FF00", "#0000FF"};
    mgr.auto_generate(colours);
    auto &vf = mgr.filaments()[0];
    vf.custom = true;
    vf.gradient_component_ids     = { 1, 2, 3 };
    vf.gradient_component_weights = { 1, 0, 1 };  // middle dropped

    std::map<unsigned int, int> hits;
    for (int i = 0; i < 100; ++i) ++hits[mgr.resolve(4, 3, i)];
    CHECK(hits[2] == 0);
    CHECK(hits[1] + hits[3] == 100);
    // 1:1 split between the two surviving ids
    CHECK(hits[1] == 50);
    CHECK(hits[3] == 50);
}

TEST_CASE("3-component gradient: all-zero weights fall back to equal split",
          "[VirtualFilamentManager][Gradient]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#00FF00", "#0000FF"};
    mgr.auto_generate(colours);
    auto &vf = mgr.filaments()[0];
    vf.custom = true;
    vf.gradient_component_ids     = { 1, 2, 3 };
    vf.gradient_component_weights = { 0, 0, 0 };  // pathological → fallback to {1,1,1}

    std::map<unsigned int, int> hits;
    for (int i = 0; i < 300; ++i) ++hits[mgr.resolve(4, 3, i)];
    CHECK(hits[1] == 100);
    CHECK(hits[2] == 100);
    CHECK(hits[3] == 100);
}

TEST_CASE("3-component gradient caps cycle length at 48",
          "[VirtualFilamentManager][Gradient]") {
    // 23:31:37 are all coprime, sum = 91 > 48 → cycle gets scaled down.
    // After scaling the cycle must be <= 48 and the layer-0/layer-cycle
    // values must agree.
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#00FF00", "#0000FF"};
    mgr.auto_generate(colours);
    auto &vf = mgr.filaments()[0];
    vf.custom = true;
    vf.gradient_component_ids     = { 1, 2, 3 };
    vf.gradient_component_weights = { 23, 31, 37 };

    // Find the cycle by sampling: the first layer N where resolve(0)==resolve(N)
    // for several consecutive layers is the cycle length.
    auto sample = [&](int start, int n) {
        std::vector<unsigned int> v;
        for (int i = 0; i < n; ++i) v.push_back(mgr.resolve(4, 3, start + i));
        return v;
    };
    const auto window0 = sample(0, 10);
    bool found_cycle = false;
    for (int cyc = 1; cyc <= 48; ++cyc) {
        if (sample(cyc, 10) == window0) { found_cycle = true; break; }
    }
    CHECK(found_cycle);
}

// ---- Manual pattern with commas / separators -------------------------------
// flatten_manual_pattern() drops only commas and leaves all other chars
// alone; physical_from_pattern_step() and is_pattern_separator() then make
// sense of what's left. These tests exercise the full route through resolve().

TEST_CASE("manual_pattern with comma separators behaves like the no-comma form",
          "[VirtualFilamentManager][ManualPattern]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#0000FF"};
    mgr.auto_generate(colours);
    auto &vf = mgr.filaments()[0];
    vf.custom = true;

    // Comma is a syntactic-sugar separator — these patterns must resolve
    // identically (both flatten to "112").
    vf.manual_pattern = "1,1,2";
    const unsigned int r0 = mgr.resolve(3, 2, 0);
    const unsigned int r1 = mgr.resolve(3, 2, 1);
    const unsigned int r2 = mgr.resolve(3, 2, 2);
    CHECK(r0 == vf.component_a);
    CHECK(r1 == vf.component_a);
    CHECK(r2 == vf.component_b);
}

TEST_CASE("normalize_manual_pattern handles letter aliases and separators",
          "[VirtualFilamentManager][ManualPattern]") {
    // resolve() reads vf.manual_pattern verbatim — only digits '1'/'2'/'3'…
    // are interpreted. User-typed forms ("AABB", "1-1-2-2", "a/a/b/b") have
    // to go through normalize_manual_pattern() first; that's what converts
    // 'a' -> '1', 'B' -> '2' and strips separators. Verify both the
    // normalization and the end-to-end resolve.
    using VFM = VirtualFilamentManager;

    CHECK(VFM::normalize_manual_pattern("AABB")     == "1122");
    CHECK(VFM::normalize_manual_pattern("a-a/b,b")  == "1122");
    CHECK(VFM::normalize_manual_pattern("1 1 2 2")  == "1122");
    CHECK(VFM::normalize_manual_pattern("12X").empty());  // invalid -> ""

    VFM mgr;
    std::vector<std::string> colours = {"#FF0000", "#0000FF"};
    mgr.auto_generate(colours);
    auto &vf = mgr.filaments()[0];
    vf.custom = true;
    vf.manual_pattern = VFM::normalize_manual_pattern("aabb");  // -> "1122"
    REQUIRE(vf.manual_pattern == "1122");
    CHECK(mgr.resolve(3, 2, 0) == vf.component_a);
    CHECK(mgr.resolve(3, 2, 1) == vf.component_a);
    CHECK(mgr.resolve(3, 2, 2) == vf.component_b);
    CHECK(mgr.resolve(3, 2, 3) == vf.component_b);
}

// ---- Run-length cap (build_capped_ab_sequence) -----------------------------
// Drives the run-length-cap branch in resolve() by setting
// local_z_max_sublayers below the larger ratio.

TEST_CASE("local_z_max_sublayers caps the longest contiguous run",
          "[VirtualFilamentManager][RunLengthCap]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#0000FF"};
    mgr.auto_generate(colours);
    auto &vf = mgr.filaments()[0];
    vf.custom = true;
    vf.ratio_a = 6;
    vf.ratio_b = 2;
    vf.local_z_max_sublayers = 2;  // strictly tighter than ratio_a=6

    // Walk one full cycle (8 layers) and verify no contiguous run exceeds 2.
    int cur_run = 1, max_run = 1;
    unsigned int prev = mgr.resolve(3, 2, 0);
    for (int i = 1; i < 16; ++i) {
        unsigned int cur = mgr.resolve(3, 2, i);
        if (cur == prev) cur_run++;
        else             cur_run = 1;
        max_run = std::max(max_run, cur_run);
        prev = cur;
    }
    CHECK(max_run <= 2);
}

TEST_CASE("local_z_max_sublayers approximately preserves a/b proportion",
          "[VirtualFilamentManager][RunLengthCap]") {
    // For infeasible ratios (one side > cap-1 multiples of the other), the
    // capped sequence is wraparound-trimmed so two adjacent cycles can't
    // exceed `cap` at the boundary. This intentionally drops a few trailing
    // emissions, so the long-run distribution is *approximately* the
    // configured ratio rather than exactly. We verify that both the cap is
    // honored and the proportion stays in the right neighborhood.
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#0000FF"};
    mgr.auto_generate(colours);
    auto &vf = mgr.filaments()[0];
    vf.custom = true;
    vf.ratio_a = 6;
    vf.ratio_b = 2;
    vf.local_z_max_sublayers = 2;

    int count_a = 0, count_b = 0;
    int cur_run = 1, max_run = 1;
    unsigned int prev = mgr.resolve(3, 2, 0);
    for (int i = 0; i < 200; ++i) {
        unsigned int r = mgr.resolve(3, 2, i);
        if (r == vf.component_a) ++count_a; else ++count_b;
        if (i > 0) {
            if (r == prev) cur_run++;
            else           cur_run = 1;
            max_run = std::max(max_run, cur_run);
        }
        prev = r;
    }
    // Cap is the load-bearing invariant — must hold strictly.
    CHECK(max_run <= 2);
    // Proportion is approximate (within 10% of nominal 75%) — algorithm
    // trims trailing emissions to keep the cap clean across cycle wraps.
    const double frac_a = double(count_a) / 200.0;
    CHECK(frac_a >= 0.65);
    CHECK(frac_a <= 0.85);
    // Both components must still appear.
    CHECK(count_b > 0);
}

TEST_CASE("local_z_max_sublayers >= max(ratio_a, ratio_b) is a no-op",
          "[VirtualFilamentManager][RunLengthCap]") {
    // When the cap is already higher than both ratios, the simple
    // layer-cycle path runs and we should observe a single contiguous
    // run of length ratio_a followed by ratio_b.
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#0000FF"};
    mgr.auto_generate(colours);
    auto &vf = mgr.filaments()[0];
    vf.custom = true;
    vf.ratio_a = 3;
    vf.ratio_b = 1;
    vf.local_z_max_sublayers = 10;  // > ratio_a

    // Cycle of 4: A A A B
    CHECK(mgr.resolve(3, 2, 0) == vf.component_a);
    CHECK(mgr.resolve(3, 2, 1) == vf.component_a);
    CHECK(mgr.resolve(3, 2, 2) == vf.component_a);
    CHECK(mgr.resolve(3, 2, 3) == vf.component_b);
    CHECK(mgr.resolve(3, 2, 4) == vf.component_a);  // wraps
}
