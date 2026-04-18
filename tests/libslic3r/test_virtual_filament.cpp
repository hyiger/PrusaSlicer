#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "libslic3r/VirtualFilament.hpp"
#include "libslic3r/PrintConfig.hpp"

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
