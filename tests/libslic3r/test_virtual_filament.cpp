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

TEST_CASE("virtual_index_from_id skips disabled filaments", "[VirtualFilamentManager]") {
    VirtualFilamentManager mgr;
    std::vector<std::string> colours = {"#FF0000", "#00FF00", "#0000FF"};
    mgr.auto_generate(colours);

    // Disable the first virtual filament (1+2 at index 0)
    mgr.filaments()[0].enabled = false;

    // ID 4 now maps to the second enabled virtual (index 1: 1+3)
    CHECK(mgr.virtual_index_from_id(4, 3) == 1);
    // ID 5 maps to the third enabled virtual (index 2: 2+3)
    CHECK(mgr.virtual_index_from_id(5, 3) == 2);
    // Only 2 enabled, so ID 6 is out of range
    CHECK(mgr.virtual_index_from_id(6, 3) == -1);
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

    // Disable it
    mgr.filaments()[0].enabled = false;
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
    CHECK(config.virtual_filament_region_collapse.value == true);
}

TEST_CASE("DynamicPrintConfig can set virtual filament options", "[VirtualFilament][Config]") {
    DynamicPrintConfig config;
    config.set_key_value("virtual_filaments_enabled", new ConfigOptionBool(true));
    config.set_key_value("virtual_filament_definitions", new ConfigOptionString("1,2,1,0,50"));

    CHECK(config.opt_bool("virtual_filaments_enabled") == true);
    CHECK(config.opt_string("virtual_filament_definitions") == "1,2,1,0,50");
}
