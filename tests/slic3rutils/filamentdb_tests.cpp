// Copyright (c) 2026
// PrusaSlicer is released under the terms of the AGPLv3 or higher
//
// Tests for the FilamentDB sync helper logic. Currently focused on the
// `config_first_string` extractor — a regression target for the bug where
// commas embedded in `filament_vendor` (e.g. "Prusa Research, a.s.") were
// being interpreted as list separators and truncating the vendor before it
// reached the create payload.

#include <catch2/catch_test_macros.hpp>

#include "slic3r/Utils/FilamentDB.hpp"

#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"

using namespace Slic3r;
using filamentdb_detail::config_first_string;

TEST_CASE("config_first_string: missing key returns empty", "[FilamentDB]")
{
    DynamicPrintConfig cfg;
    REQUIRE(config_first_string(cfg, "filament_vendor").empty());
    REQUIRE(config_first_string(cfg, "filament_type").empty());
}

TEST_CASE("config_first_string: coString preserves commas", "[FilamentDB]")
{
    // filament_vendor is coString — a single std::string. Embedded commas are
    // real data (e.g. legal entity suffixes) and must not be split.
    DynamicPrintConfig cfg;
    cfg.set_key_value("filament_vendor", new ConfigOptionString("Prusa Research, a.s."));

    REQUIRE(config_first_string(cfg, "filament_vendor") == "Prusa Research, a.s.");
}

TEST_CASE("config_first_string: coString preserves semicolons", "[FilamentDB]")
{
    DynamicPrintConfig cfg;
    cfg.set_key_value("filament_vendor", new ConfigOptionString("Vendor; Inc."));
    REQUIRE(config_first_string(cfg, "filament_vendor") == "Vendor; Inc.");
}

TEST_CASE("config_first_string: coString empty value yields empty string", "[FilamentDB]")
{
    DynamicPrintConfig cfg;
    cfg.set_key_value("filament_vendor", new ConfigOptionString(""));
    REQUIRE(config_first_string(cfg, "filament_vendor").empty());
}

TEST_CASE("config_first_string: coStrings returns first entry verbatim", "[FilamentDB]")
{
    // filament_type is coStrings — per-extruder. For a single filament preset
    // only [0] is set, but multi-extruder configs may stash multiple. We always
    // take values.front() and never split it.
    DynamicPrintConfig cfg;
    auto *opt = new ConfigOptionStrings();
    opt->values = { "PLA", "PETG" };
    cfg.set_key_value("filament_type", opt);

    REQUIRE(config_first_string(cfg, "filament_type") == "PLA");
}

TEST_CASE("config_first_string: coStrings empty vector yields empty string", "[FilamentDB]")
{
    DynamicPrintConfig cfg;
    cfg.set_key_value("filament_type", new ConfigOptionStrings());
    REQUIRE(config_first_string(cfg, "filament_type").empty());
}

TEST_CASE("config_first_string: coStrings preserves embedded commas in [0]", "[FilamentDB]")
{
    DynamicPrintConfig cfg;
    auto *opt = new ConfigOptionStrings();
    opt->values = { "PLA, modified" };
    cfg.set_key_value("filament_type", opt);

    REQUIRE(config_first_string(cfg, "filament_type") == "PLA, modified");
}

TEST_CASE("config_first_string: wrong type returns empty", "[FilamentDB]")
{
    // If a caller passes a key whose option is a non-string type, return empty
    // rather than serializing-and-truncating. Use a known-existing numeric key.
    DynamicPrintConfig cfg;
    cfg.set_key_value("filament_diameter", new ConfigOptionFloats({ 1.75 }));
    REQUIRE(config_first_string(cfg, "filament_diameter").empty());
}
