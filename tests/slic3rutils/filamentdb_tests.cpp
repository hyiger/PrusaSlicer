// Copyright (c) 2026
// PrusaSlicer is released under the terms of the AGPLv3 or higher
//
// Tests for the FilamentDB sync helper logic. Currently focused on the
// `config_first_string` extractor — a regression target for the bug where
// commas embedded in `filament_vendor` (e.g. "Prusa Research, a.s.") were
// being interpreted as list separators and truncating the vendor before it
// reached the create payload.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include "slic3r/Utils/FilamentDB.hpp"

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"

using namespace Slic3r;
using filamentdb_detail::config_first_string;
using filamentdb_detail::extract_json_string;
using filamentdb_detail::bundle_to_candidates;
using Catch::Approx;
namespace fs = boost::filesystem;

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

// ---- parse_filamentdb_bundle (the INI parser used to ingest the
//      FilamentDB server's `/api/filaments/prusaslicer` response) ------------

TEST_CASE("parse_filamentdb_bundle: empty input yields no presets", "[FilamentDB]")
{
    REQUIRE(Slic3r::parse_filamentdb_bundle("").empty());
    REQUIRE(Slic3r::parse_filamentdb_bundle("\n\n#just a comment\n").empty());
}

TEST_CASE("parse_filamentdb_bundle: single section with key/value pairs", "[FilamentDB]")
{
    const std::string ini =
        "[filament:My PLA]\n"
        "filament_type = PLA\n"
        "filament_vendor = Acme\n"
        "temperature = 210\n";

    auto presets = Slic3r::parse_filamentdb_bundle(ini);
    REQUIRE(presets.size() == 1);
    CHECK(presets[0].name == "My PLA");
    CHECK(presets[0].type == "PLA");
    CHECK(presets[0].vendor == "Acme");
    REQUIRE(presets[0].config_pairs.size() == 3);
    // Order is preserved.
    CHECK(presets[0].config_pairs[0].first  == "filament_type");
    CHECK(presets[0].config_pairs[2].second == "210");
}

TEST_CASE("parse_filamentdb_bundle: multiple sections", "[FilamentDB]")
{
    const std::string ini =
        "[filament:A]\nfilament_type = PLA\n"
        "[filament:B]\nfilament_type = PETG\n";
    auto presets = Slic3r::parse_filamentdb_bundle(ini);
    REQUIRE(presets.size() == 2);
    CHECK(presets[0].name == "A");
    CHECK(presets[1].name == "B");
    CHECK(presets[1].type == "PETG");
}

TEST_CASE("parse_filamentdb_bundle: skips abstract presets", "[FilamentDB]")
{
    // PrusaSlicer convention: section names wrapped in '*…*' are inheritance
    // bases ("abstract") and should not appear as user-facing presets.
    const std::string ini =
        "[filament:*PLA*]\nfilament_type = PLA\n"  // dropped
        "[filament:Real PLA]\nfilament_vendor = Real\n";
    auto presets = Slic3r::parse_filamentdb_bundle(ini);
    REQUIRE(presets.size() == 1);
    CHECK(presets[0].name == "Real PLA");
}

TEST_CASE("parse_filamentdb_bundle: skips comments and blank lines", "[FilamentDB]")
{
    const std::string ini =
        "# Generated by FilamentDB\n"
        "\n"
        "[filament:Demo]\n"
        "# inline comment\n"
        "filament_type = PLA\n"
        "\n";
    auto presets = Slic3r::parse_filamentdb_bundle(ini);
    REQUIRE(presets.size() == 1);
    REQUIRE(presets[0].config_pairs.size() == 1);
    CHECK(presets[0].config_pairs[0].first == "filament_type");
}

TEST_CASE("parse_filamentdb_bundle: non-filament sections terminate tracking", "[FilamentDB]")
{
    // A section that doesn't start with "filament:" must reset the active
    // preset pointer so that any keys *after* it aren't mis-attributed.
    const std::string ini =
        "[filament:Real]\nfilament_type = PLA\n"
        "[print:My Print]\nlayer_height = 0.2\n"      // ignored
        "perimeter_extrusion_width = 0.45\n";          // also ignored (no active filament)
    auto presets = Slic3r::parse_filamentdb_bundle(ini);
    REQUIRE(presets.size() == 1);
    CHECK(presets[0].config_pairs.size() == 1);
    CHECK(presets[0].config_pairs[0].first == "filament_type");
}

TEST_CASE("parse_filamentdb_bundle: pre-section keys are ignored", "[FilamentDB]")
{
    // Keys that appear before any [filament:...] header have no current
    // preset to attach to and are silently dropped.
    const std::string ini =
        "filament_type = junk\n"
        "[filament:Good]\nfilament_type = PETG\n";
    auto presets = Slic3r::parse_filamentdb_bundle(ini);
    REQUIRE(presets.size() == 1);
    REQUIRE(presets[0].config_pairs.size() == 1);
    CHECK(presets[0].type == "PETG");
}

TEST_CASE("parse_filamentdb_bundle: trims whitespace around keys/values/names", "[FilamentDB]")
{
    const std::string ini =
        "[filament:   Spaced   ]\n"
        "  filament_vendor   =   Acme  \n";
    auto presets = Slic3r::parse_filamentdb_bundle(ini);
    REQUIRE(presets.size() == 1);
    CHECK(presets[0].name == "Spaced");
    REQUIRE(presets[0].config_pairs.size() == 1);
    CHECK(presets[0].config_pairs[0].first  == "filament_vendor");
    CHECK(presets[0].config_pairs[0].second == "Acme");
    CHECK(presets[0].vendor == "Acme");
}

TEST_CASE("parse_filamentdb_bundle: vendor with embedded comma survives", "[FilamentDB]")
{
    // Regression target: the parser must NOT split values on commas. This is
    // the same family of bug as the config_first_string corruption.
    const std::string ini =
        "[filament:Czech PLA]\n"
        "filament_vendor = Prusa Research, a.s.\n";
    auto presets = Slic3r::parse_filamentdb_bundle(ini);
    REQUIRE(presets.size() == 1);
    CHECK(presets[0].vendor == "Prusa Research, a.s.");
    REQUIRE(presets[0].config_pairs.size() == 1);
    CHECK(presets[0].config_pairs[0].second == "Prusa Research, a.s.");
}

TEST_CASE("parse_filamentdb_bundle: lines with no '=' are ignored within a section", "[FilamentDB]")
{
    const std::string ini =
        "[filament:Demo]\n"
        "garbage line without equals\n"
        "filament_type = PLA\n";
    auto presets = Slic3r::parse_filamentdb_bundle(ini);
    REQUIRE(presets.size() == 1);
    REQUIRE(presets[0].config_pairs.size() == 1);
    CHECK(presets[0].config_pairs[0].first == "filament_type");
}

// ---- extract_json_string (reads the #867 sync response fields:
//      matchedBy / matchedName / filamentId / error / sentName) --------------

TEST_CASE("extract_json_string: missing key returns empty", "[FilamentDB]")
{
    REQUIRE(extract_json_string("{\"a\":\"1\"}", "b").empty());
    REQUIRE(extract_json_string("", "a").empty());
}

TEST_CASE("extract_json_string: simple value", "[FilamentDB]")
{
    REQUIRE(extract_json_string("{\"matchedBy\":\"name\"}", "matchedBy") == "name");
}

TEST_CASE("extract_json_string: stops at the closing quote, ignores trailing keys", "[FilamentDB]")
{
    const std::string body = "{\"filamentId\":\"6630ab\",\"matchedName\":\"PLA Galaxy\"}";
    REQUIRE(extract_json_string(body, "filamentId") == "6630ab");
    REQUIRE(extract_json_string(body, "matchedName") == "PLA Galaxy");
}

TEST_CASE("extract_json_string: unescapes embedded quotes and backslashes", "[FilamentDB]")
{
    // value: She said "hi" \ done
    const std::string body = "{\"matchedName\":\"She said \\\"hi\\\" \\\\ done\"}";
    REQUIRE(extract_json_string(body, "matchedName") == "She said \"hi\" \\ done");
}

TEST_CASE("extract_json_string: unescapes \\n and \\t", "[FilamentDB]")
{
    REQUIRE(extract_json_string("{\"k\":\"a\\nb\\tc\"}", "k") == "a\nb\tc");
}

TEST_CASE("extract_json_string: null value yields empty", "[FilamentDB]")
{
    REQUIRE(extract_json_string("{\"matchedBy\":null}", "matchedBy").empty());
}

TEST_CASE("extract_json_string: numeric (non-string) value yields empty", "[FilamentDB]")
{
    REQUIRE(extract_json_string("{\"http\":409}", "http").empty());
}

TEST_CASE("extract_json_string: leading quote in search guards against suffix false-match", "[FilamentDB]")
{
    // Searching "name" must NOT match inside "matchedName" — the search includes
    // the opening quote ("name":), which only matches a key literally named name.
    const std::string body = "{\"matchedName\":\"X\"}";
    REQUIRE(extract_json_string(body, "name").empty());
}

TEST_CASE("extract_json_string: parses a realistic 409 name_id_mismatch body", "[FilamentDB]")
{
    const std::string body =
        "{\"error\":\"name_id_mismatch\","
        "\"message\":\"filamentdb_id resolves to ...\","
        "\"matchedBy\":\"id\",\"filamentId\":\"663012ab34cd\","
        "\"matchedName\":\"Fibreheart PPA\",\"sentName\":\"SirayaTech Fibreheart PPA\"}";
    CHECK(extract_json_string(body, "error")       == "name_id_mismatch");
    CHECK(extract_json_string(body, "filamentId")  == "663012ab34cd");
    CHECK(extract_json_string(body, "matchedName") == "Fibreheart PPA");
    CHECK(extract_json_string(body, "sentName")    == "SirayaTech Fibreheart PPA");
}

TEST_CASE("extract_json_string: tolerates whitespace around the colon", "[FilamentDB]")
{
    CHECK(extract_json_string("{\"error\": \"name_id_mismatch\"}", "error")  == "name_id_mismatch");
    CHECK(extract_json_string("{\"error\" : \"name_id_mismatch\"}", "error") == "name_id_mismatch");
    CHECK(extract_json_string("{\"error\"\n:\n  \"x\"}", "error")            == "x");
    // Pretty-printed multi-field body.
    const std::string pretty =
        "{\n  \"matchedBy\": \"id\",\n  \"filamentId\" : \"abc123\",\n  \"matchedName\": \"PLA\"\n}";
    CHECK(extract_json_string(pretty, "matchedBy")   == "id");
    CHECK(extract_json_string(pretty, "filamentId")  == "abc123");
    CHECK(extract_json_string(pretty, "matchedName") == "PLA");
}

// ---- bundle_to_candidates (turns the export INI bundle into "Link to existing"
//      choices — the #36 Phase 2 no-match flow) ------------------------------

TEST_CASE("bundle_to_candidates: empty bundle yields no candidates", "[FilamentDB]")
{
    REQUIRE(bundle_to_candidates("").empty());
    REQUIRE(bundle_to_candidates("[filament:No Id]\nfilament_type = PLA\n").empty());
}

TEST_CASE("bundle_to_candidates: extracts id/name/vendor/type", "[FilamentDB]")
{
    const std::string ini =
        "[filament:Galaxy PLA]\n"
        "filament_vendor = Acme\n"
        "filament_type = PLA\n"
        "filamentdb_id = 663012ab34cd\n";
    auto c = bundle_to_candidates(ini);
    REQUIRE(c.size() == 1);
    CHECK(c[0].id     == "663012ab34cd");
    CHECK(c[0].name   == "Galaxy PLA");
    CHECK(c[0].vendor == "Acme");
    CHECK(c[0].type   == "PLA");
}

TEST_CASE("bundle_to_candidates: sections without a filamentdb_id are skipped", "[FilamentDB]")
{
    const std::string ini =
        "[filament:Has Id]\nfilament_type = PLA\nfilamentdb_id = aaa111\n"
        "[filament:No Id]\nfilament_type = PETG\n";  // dropped — can't link without an id
    auto c = bundle_to_candidates(ini);
    REQUIRE(c.size() == 1);
    CHECK(c[0].name == "Has Id");
    CHECK(c[0].id   == "aaa111");
}

TEST_CASE("bundle_to_candidates: de-dupes by id, keeping the shortest (base) name", "[FilamentDB]")
{
    // A multi-nozzle export: three sections, one shared id. The base name is the
    // shortest; the per-nozzle suffixes only lengthen it.
    const std::string ini =
        "[filament:PA-CF 0.6 Diamondback]\nfilamentdb_id = deadbeef\n"
        "[filament:PA-CF]\nfilamentdb_id = deadbeef\n"
        "[filament:PA-CF 0.4 Brass HF]\nfilamentdb_id = deadbeef\n";
    auto c = bundle_to_candidates(ini);
    REQUIRE(c.size() == 1);
    CHECK(c[0].id   == "deadbeef");
    CHECK(c[0].name == "PA-CF");
}

TEST_CASE("bundle_to_candidates: distinct ids preserve first-seen order", "[FilamentDB]")
{
    const std::string ini =
        "[filament:Zeta]\nfilamentdb_id = id_z\n"
        "[filament:Alpha]\nfilamentdb_id = id_a\n";
    auto c = bundle_to_candidates(ini);
    REQUIRE(c.size() == 2);
    CHECK(c[0].name == "Zeta");   // bundle order preserved (server sorts by name)
    CHECK(c[1].name == "Alpha");
    CHECK(c[0].id   == "id_z");
    CHECK(c[1].id   == "id_a");
}

// ---- #872 per-nozzle presets: the filamentdb_nozzle hint ----------------------

TEST_CASE("parse_nozzle_hint: diameter, type and the HF tail", "[FilamentDB]")
{
    using filamentdb_detail::parse_nozzle_hint;

    auto brass = parse_nozzle_hint("0.4 Brass");
    CHECK(brass.valid());
    CHECK(brass.diameter == Approx(0.4));
    CHECK(brass.type == "Brass");
    CHECK_FALSE(brass.high_flow);

    auto indx = parse_nozzle_hint("0.4 Other HF");
    CHECK(indx.diameter == Approx(0.4));
    CHECK(indx.type == "Other");
    CHECK(indx.high_flow);

    // A type with a space, and a lower-case tail (the server's / HF$/i).
    auto wc = parse_nozzle_hint(" 0.6 Tungsten Carbide hf ");
    CHECK(wc.diameter == Approx(0.6));
    CHECK(wc.type == "Tungsten Carbide");
    CHECK(wc.high_flow);

    // No type: the server's nozzleSuffix leaves it out.
    auto bare = parse_nozzle_hint("0.4 HF");
    CHECK(bare.diameter == Approx(0.4));
    CHECK(bare.type.empty());
    CHECK(bare.high_flow);
    CHECK(parse_nozzle_hint("0.25").diameter == Approx(0.25));
}

TEST_CASE("parse_nozzle_hint: no leading diameter is no hint", "[FilamentDB]")
{
    using filamentdb_detail::parse_nozzle_hint;
    for (const char *h : { "", "   ", "Brass", "HF", "Brass HF", "0.4.1 Brass", ". Brass", "-0.4 Brass", "0 Brass" }) {
        INFO(h);
        const auto hint = parse_nozzle_hint(h);
        CHECK_FALSE(hint.valid());
        CHECK_FALSE(hint.high_flow);
        CHECK(hint.type.empty());
    }
}

TEST_CASE("nozzle_hint reads the registered option, trimmed", "[FilamentDB]")
{
    DynamicPrintConfig cfg;
    CHECK(filamentdb_detail::nozzle_hint(cfg).empty());
    // Registered: set_deserialize would throw for an unknown key.
    cfg.set_deserialize_strict("filamentdb_nozzle", " 0.4 Other HF ");
    CHECK(filamentdb_detail::nozzle_hint(cfg) == "0.4 Other HF");
}

TEST_CASE("hint_matches_nozzle: diameter and high flow both count", "[FilamentDB]")
{
    using filamentdb_detail::hint_matches_nozzle;
    using filamentdb_detail::parse_nozzle_hint;
    CHECK(hint_matches_nozzle(parse_nozzle_hint("0.4 Other HF"), 0.4, true));
    CHECK_FALSE(hint_matches_nozzle(parse_nozzle_hint("0.4 Other HF"), 0.4, false));
    CHECK_FALSE(hint_matches_nozzle(parse_nozzle_hint("0.4 Brass"), 0.4, true));
    CHECK_FALSE(hint_matches_nozzle(parse_nozzle_hint("0.4 Brass"), 0.6, false));
    CHECK_FALSE(hint_matches_nozzle(parse_nozzle_hint(""), 0.4, false));
}

TEST_CASE("strip_nozzle_suffix: the record behind a per-nozzle preset", "[FilamentDB]")
{
    using filamentdb_detail::strip_nozzle_suffix;
    CHECK(strip_nozzle_suffix("Pro PCTG Cobalt Blue 0.4 Other HF", "0.4 Other HF") == "Pro PCTG Cobalt Blue");
    CHECK(strip_nozzle_suffix("Pro PCTG Cobalt Blue 0.4 Other HF", " 0.4 Other HF ") == "Pro PCTG Cobalt Blue");
    // Not a suffixed name, no hint, or nothing left: unchanged.
    CHECK(strip_nozzle_suffix("My PCTG", "0.4 Other HF") == "My PCTG");
    CHECK(strip_nozzle_suffix("PLA 0.4 Brass", "") == "PLA 0.4 Brass");
    CHECK(strip_nozzle_suffix("0.4 Brass", "0.4 Brass") == "0.4 Brass");
    CHECK(strip_nozzle_suffix("PLA0.4 Brass", "0.4 Brass") == "PLA0.4 Brass");
    // The raw slice: "X " stays distinct from "X" (GH #1116).
    CHECK(strip_nozzle_suffix("X  0.4 Brass", "0.4 Brass") == "X ");
}

// ---- calibration lookup ----------------------------------------------------------

TEST_CASE("calibration_url: high flow always, type and printer when given", "[FilamentDB]")
{
    FilamentCalibrationRequest req;
    req.nozzle_diameter = 0.4;
    req.high_flow       = true;
    const std::string id_url = filamentdb_detail::calibration_url("http://h:3456/", "6a9caaba3379b2e75836422e", req);
    CHECK(id_url == "http://h:3456/api/filaments/6a9caaba3379b2e75836422e/calibration?nozzle_diameter=0.4&high_flow=1");

    req.high_flow   = false;
    req.nozzle_type = "Tungsten Carbide";
    req.printer     = "Prusa Core One INDX";
    const std::string url = filamentdb_detail::calibration_url("http://h:3456", "PLA 100%", req);
    CHECK(url.find("/api/filaments/PLA%20100%25/calibration?") != std::string::npos);
    CHECK(url.find("&high_flow=0") != std::string::npos);
    CHECK(url.find("&nozzle_type=Tungsten%20Carbide") != std::string::npos);
    CHECK(url.find("&printer=Prusa%20Core%20One%20INDX") != std::string::npos);
}

TEST_CASE("parse_calibration_response: values, nulls, nozzle and printer", "[FilamentDB]")
{
    const std::string body = R"({"filament":"Push Plastic PMMA","nozzle":{"diameter":0.4,"name":"INDX","highFlow":true},)"
                             R"("printer":"Prusa Core One INDX","bedType":null,"calibration":{"pressureAdvance":null,)"
                             R"("maxVolumetricSpeed":3.6,"extrusionMultiplier":1,"retractLength":0.8,"retractSpeed":null,)"
                             R"("retractLift":null,"nozzleTemp":260}})";
    const FilamentCalibration cal = filamentdb_detail::parse_calibration_response(body);
    CHECK(cal.found);
    CHECK(cal.pressure_advance == -1);
    CHECK(cal.max_volumetric_speed == Approx(3.6));
    CHECK(cal.extrusion_multiplier == Approx(1.0));
    CHECK(cal.retract_length == Approx(0.8));
    CHECK(cal.retract_speed == -1);
    CHECK(cal.nozzle_name == "INDX");
    CHECK(cal.printer == "Prusa Core One INDX");

    const FilamentCalibration shared = filamentdb_detail::parse_calibration_response(
        R"({"filament":"X","nozzle":{"name":"INDX"},"printer":null,"calibration":{"pressureAdvance":0.05}})");
    CHECK(shared.found);
    CHECK(shared.printer.empty());
    CHECK(shared.pressure_advance == Approx(0.05));

    CHECK_FALSE(filamentdb_detail::parse_calibration_response("not json").found);
    CHECK_FALSE(filamentdb_detail::parse_calibration_response("[1,2]").found);
}

// The two printers of the 2026-10-21 audit, as GET /api/printers returns them.
static const char *PRINTERS_JSON = R"([
 {"_id":"6a1a7bee677d648e9ba9cd14","name":"Bambu Labs H2D","manufacturer":"Bambu Labs","_deletedAt":null,
  "installedNozzles":[
   {"_id":"n1","name":"Diamondback 0.4","diameter":0.4,"type":"Diamondback","highFlow":false,"_deletedAt":null},
   {"_id":"n2","name":"Diamondback 0.6","diameter":0.6,"type":"Diamondback","highFlow":false},
   {"_id":"n3","name":"WC HF 0.4","diameter":0.4,"type":"Tungsten Carbide","highFlow":true}]},
 {"_id":"6a6bd2980b602d3371709beb","name":"Prusa Core One INDX","manufacturer":"Prusa",
  "installedNozzles":[{"_id":"n4","name":"INDX","diameter":0.4,"type":"Other","highFlow":true,"hardened":false}]},
 {"_id":"6a6bd2980b602d3371709bff","name":"Old Printer","_deletedAt":"2026-01-01T00:00:00.000Z","installedNozzles":[]},
 {"_id":"6a6bd2980b602d3371709c00","name":"Unpopulated","installedNozzles":["6a6bd2b20b602d3371709bec"]}
])";

static std::vector<FilamentDBPrinter> audit_printers()
{
    std::vector<FilamentDBPrinter> printers;
    REQUIRE(filamentdb_detail::parse_printers_json(PRINTERS_JSON, printers));
    return printers;
}

TEST_CASE("parse_printers_json: live printers with their populated nozzles", "[FilamentDB]")
{
    const auto printers = audit_printers();
    REQUIRE(printers.size() == 3); // the trashed one is skipped
    CHECK(printers[0].name == "Bambu Labs H2D");
    REQUIRE(printers[0].nozzles.size() == 3);
    CHECK(printers[0].nozzles[2].name == "WC HF 0.4");
    CHECK(printers[0].nozzles[2].type == "Tungsten Carbide");
    CHECK(printers[0].nozzles[2].high_flow);
    CHECK(printers[1].id == "6a6bd2980b602d3371709beb");
    REQUIRE(printers[1].nozzles.size() == 1);
    CHECK(printers[1].nozzles[0].diameter == Approx(0.4));
    CHECK(printers[2].name == "Unpopulated");
    CHECK(printers[2].nozzles.empty()); // a bare id has nothing to match on

    std::vector<FilamentDBPrinter> none;
    CHECK_FALSE(filamentdb_detail::parse_printers_json("{\"error\":\"x\"}", none));
    CHECK_FALSE(filamentdb_detail::parse_printers_json("<html>", none));
}

TEST_CASE("resolve_printer: physical printer name, then preset names", "[FilamentDB]")
{
    using filamentdb_detail::resolve_printer;
    const auto printers = audit_printers();

    SlicerPrinterIdentity who;
    who.preset_names = { "Prusa CORE One INDX 8T HF0.4 nozzle - Copy", "Prusa CORE One INDX 8T HF0.4 nozzle" };
    const FilamentDBPrinter *p = resolve_printer(printers, who);
    REQUIRE(p != nullptr);
    CHECK(p->name == "Prusa Core One INDX");

    // A user preset named freely falls back to its system parent's name.
    who.preset_names = { "My INDX", "Prusa CORE One INDX 8T HF0.4 nozzle" };
    REQUIRE(resolve_printer(printers, who) != nullptr);

    // The physical printer wins, exactly or ignoring case and spaces.
    who.physical_printer = " bambu labs h2d";
    p = resolve_printer(printers, who);
    REQUIRE(p != nullptr);
    CHECK(p->name == "Bambu Labs H2D");

    // A physical printer Filament DB doesn't know defers to the preset names.
    who.physical_printer = "Home";
    p = resolve_printer(printers, who);
    REQUIRE(p != nullptr);
    CHECK(p->name == "Prusa Core One INDX");

    who = {};
    who.preset_names = { "Original Prusa MK4S HF0.4 nozzle" };
    CHECK(resolve_printer(printers, who) == nullptr);
    CHECK(resolve_printer({}, who) == nullptr);
}

TEST_CASE("resolve_printer: the longest name at a word boundary, ties match nothing", "[FilamentDB]")
{
    using filamentdb_detail::resolve_printer;
    std::vector<FilamentDBPrinter> printers = {
        { "a", "Prusa Core One", {} },
        { "b", "Prusa Core One INDX", {} },
    };
    SlicerPrinterIdentity who;
    who.preset_names = { "Prusa CORE One INDX 8T HF0.4 nozzle" };
    REQUIRE(resolve_printer(printers, who) != nullptr);
    CHECK(resolve_printer(printers, who)->id == "b");

    who.preset_names = { "Prusa CORE One HF0.4 nozzle" };
    CHECK(resolve_printer(printers, who)->id == "a");

    // Not at a word boundary.
    who.preset_names = { "Prusa Core One INDXL" };
    CHECK(resolve_printer(printers, who)->id == "a");
    printers.pop_back();
    printers.push_back({ "c", "Prusa Core On", {} });
    who.preset_names = { "Prusa Core Onex" };
    CHECK(resolve_printer(printers, who) == nullptr);

    // Two printers whose names differ only in case: ambiguous.
    printers = { { "x", "Core One", {} }, { "y", "core one", {} } };
    who.preset_names = { "CORE ONE 0.4 nozzle" };
    CHECK(resolve_printer(printers, who) == nullptr);
    // An exact spelling still picks one.
    who.preset_names = { "core one" };
    CHECK(resolve_printer(printers, who)->id == "y");
}

TEST_CASE("installed_nozzle_type: the one nozzle of that size and flow", "[FilamentDB]")
{
    using filamentdb_detail::installed_nozzle_type;
    const auto printers = audit_printers();
    CHECK(installed_nozzle_type(printers[1], 0.4, true) == "Other");
    CHECK(installed_nozzle_type(printers[0], 0.4, true) == "Tungsten Carbide");
    CHECK(installed_nozzle_type(printers[0], 0.4, false) == "Diamondback");
    CHECK(installed_nozzle_type(printers[1], 0.6, true).empty());

    FilamentDBPrinter two = printers[1];
    two.nozzles.push_back({ "n9", "Second", "Brass", 0.4, true });
    CHECK(installed_nozzle_type(two, 0.4, true).empty());
}

TEST_CASE("calibration_from_other_printer: rows tuned on another machine", "[FilamentDB]")
{
    using filamentdb_detail::calibration_from_other_printer;
    const auto               printers = audit_printers();
    const FilamentDBPrinter *indx     = &printers[1];

    FilamentCalibration cal;
    cal.found = true;

    // Scoped to the H2D.
    cal.printer     = "Bambu Labs H2D";
    cal.nozzle_name = "WC HF 0.4";
    CHECK(calibration_from_other_printer(cal, printers, indx, indx->name));
    // Scoped to this printer.
    cal.printer     = "Prusa Core One INDX";
    cal.nozzle_name = "INDX";
    CHECK_FALSE(calibration_from_other_printer(cal, printers, indx, indx->name));
    // Shared, but on a nozzle installed on the H2D.
    cal.printer     = "";
    cal.nozzle_name = "WC HF 0.4";
    CHECK(calibration_from_other_printer(cal, printers, indx, indx->name));
    // Shared, on this printer's nozzle, or on a nozzle installed nowhere.
    cal.nozzle_name = "INDX";
    CHECK_FALSE(calibration_from_other_printer(cal, printers, indx, indx->name));
    cal.nozzle_name = "0.4mm HF";
    CHECK_FALSE(calibration_from_other_printer(cal, printers, indx, indx->name));

    // Printer not found in Filament DB: shared rows are kept, scoped ones are not.
    cal.nozzle_name = "WC HF 0.4";
    CHECK_FALSE(calibration_from_other_printer(cal, printers, nullptr, "Prusa CORE One INDX 8T HF0.4 nozzle"));
    cal.printer = "Bambu Labs H2D";
    CHECK(calibration_from_other_printer(cal, printers, nullptr, "Prusa CORE One INDX 8T HF0.4 nozzle"));
    CHECK(calibration_from_other_printer(cal, printers, nullptr, ""));
    // ... unless it is scoped to the name we sent.
    CHECK_FALSE(calibration_from_other_printer(cal, printers, nullptr, " bambu labs h2d "));
}

// ---- NFC scan: which preset a scanned filament selects ---------------------------

TEST_CASE("pick_scanned_preset: the sibling for the printer's nozzle", "[FilamentDB]")
{
    using filamentdb_detail::pick_scanned_preset;
    using filamentdb_detail::ScanPresetCandidate;
    const std::string id = "6a1a7bef677d648e9ba9cd26";
    const std::vector<ScanPresetCandidate> presets = {
        { "Generic PCTG", "", "", true },
        { "Pro PCTG Cobalt Blue 0.4 Other HF", id, "0.4 Other HF", true },
        { "Pro PCTG Cobalt Blue 0.4 Tungsten Carbide HF", id, "0.4 Tungsten Carbide HF", true },
        { "Pro PCTG Cobalt Blue 0.6 Diamondback", id, "0.6 Diamondback", false },
    };
    int asked = 0;
    auto indx_type = [&asked]() { ++asked; return std::string("Other"); };

    // INDX HF0.4: two siblings fit; the INDX's installed nozzle decides.
    CHECK(pick_scanned_preset(presets, id, "Pro PCTG Cobalt Blue", 0.4, true, indx_type) == 1);
    CHECK(asked == 1);
    CHECK(pick_scanned_preset(presets, id, "Pro PCTG Cobalt Blue", 0.4, true,
                              [] { return std::string("tungsten carbide"); }) == 2);
    // One sibling fits: no need to ask.
    CHECK(pick_scanned_preset(presets, id, "Pro PCTG Cobalt Blue", 0.6, false, indx_type) == 3);
    CHECK(asked == 1);
    // Two fit and the printer's nozzle is unknown: the first compatible one.
    CHECK(pick_scanned_preset(presets, id, "Pro PCTG Cobalt Blue", 0.4, true, [] { return std::string(); }) == 1);
    CHECK(pick_scanned_preset(presets, id, "Pro PCTG Cobalt Blue", 0.4, true, nullptr) == 1);
}

TEST_CASE("pick_scanned_preset: family by id or by name, and the fallbacks", "[FilamentDB]")
{
    using filamentdb_detail::pick_scanned_preset;
    using filamentdb_detail::ScanPresetCandidate;
    const std::string id = "6a9caaba3379b2e75836422e";
    std::vector<ScanPresetCandidate> presets = {
        { "PMMA", id, "", true },                                     // left from before the fan-out
        { "Push Plastic PMMA 0.4 Other HF", "", "0.4 Other HF", false }, // no id: matched by name
        { "Renamed PMMA (INDX)", id, "0.6 Diamondback", true },          // renamed: matched by id
    };
    CHECK(pick_scanned_preset(presets, id, "Push Plastic PMMA", 0.4, true, nullptr) == 1);
    CHECK(pick_scanned_preset(presets, id, "Push Plastic PMMA", 0.6, false, nullptr) == 2);
    // No sibling for a 0.25 nozzle: a preset under the scanned name, else a
    // compatible sibling, else any sibling.
    CHECK(pick_scanned_preset(presets, id, "PMMA", 0.25, false, nullptr) == 0);
    CHECK(pick_scanned_preset(presets, id, "Push Plastic PMMA", 0.25, false, nullptr) == 2);
    presets[2].compatible = false;
    CHECK(pick_scanned_preset(presets, id, "Push Plastic PMMA", 0.25, false, nullptr) == 1);

    // A filament with a single preset is selected by name, as before.
    const std::vector<ScanPresetCandidate> single = { { "Galaxy PLA", "abc", "", true } };
    CHECK(pick_scanned_preset(single, "abc", "Galaxy PLA", 0.4, false, nullptr) == 0);
    CHECK(pick_scanned_preset(single, "", "Unknown", 0.4, false, nullptr) == -1);
    // Another filament's siblings don't count.
    const std::vector<ScanPresetCandidate> other = { { "PLA 0.4 Brass", "zzz", "0.4 Brass", true } };
    CHECK(pick_scanned_preset(other, "abc", "PETG", 0.4, false, nullptr) == -1);
}

// ---- the hint as preset data: load, save, clone --------------------------------

namespace {

class TempDataDir
{
public:
    TempDataDir() : m_old(Slic3r::data_dir())
    {
        m_dir = fs::temp_directory_path() / fs::unique_path("fdb-%%%%-%%%%");
        fs::create_directories(m_dir);
        Slic3r::set_data_dir(m_dir.string());
    }
    ~TempDataDir()
    {
        Slic3r::set_data_dir(m_old);
        boost::system::error_code ec;
        fs::remove_all(m_dir, ec);
    }
    const fs::path &dir() const { return m_dir; }

private:
    std::string m_old;
    fs::path    m_dir;
};

void write_file(const fs::path &path, const std::string &text)
{
    boost::nowide::ofstream f(path.string());
    f << text;
}

std::string binding(const Preset &p, const char *key) { return p.config.opt_string(key); }

} // namespace

TEST_CASE("filamentdb_nozzle survives loading and travels with filamentdb_id on save", "[FilamentDB]")
{
    TempDataDir  data;
    AppConfig    app{AppConfig::EAppMode::Editor};
    PresetBundle bundle;
    bundle.setup_directories();
    write_file(data.dir() / "filament" / "PCTG 0.4 Other HF.ini",
               "filament_type = PCTG\nfilamentdb_id = 6a1a7bef677d648e9ba9cd26\nfilamentdb_nozzle = 0.4 Other HF\n");
    write_file(data.dir() / "filament" / "PMMA 0.6 Diamondback.ini",
               "filament_type = PMMA\nfilamentdb_id = 6a9caaba3379b2e75836422e\nfilamentdb_nozzle = 0.6 Diamondback\n");
    app.set("filamentdb_url", "");
    app.set("presets", "filament", "PCTG 0.4 Other HF");
    bundle.load_presets(app, ForwardCompatibilitySubstitutionRule::EnableSystemSilent);

    PresetCollection &filaments = bundle.filaments;
    REQUIRE(filaments.get_selected_preset_name() == "PCTG 0.4 Other HF");
    CHECK(binding(filaments.get_selected_preset(), "filamentdb_nozzle") == "0.4 Other HF");

    // In place: kept, and written back out.
    filaments.save_current_preset("PCTG 0.4 Other HF");
    {
        PresetBundle again;
        again.load_presets(app, ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
        const Preset *p = again.filaments.find_preset("PCTG 0.4 Other HF");
        REQUIRE(p != nullptr);
        CHECK(binding(*p, "filamentdb_nozzle") == "0.4 Other HF");
    }

    // A clone is a new filament: no id, and no hint without it.
    filaments.save_current_preset("My PCTG");
    const Preset *clone = filaments.find_preset("My PCTG");
    REQUIRE(clone != nullptr);
    CHECK(binding(*clone, "filamentdb_id").empty());
    CHECK(binding(*clone, "filamentdb_nozzle").empty());

    // Overwriting another preset keeps that preset's own binding.
    filaments.select_preset_by_name("PCTG 0.4 Other HF", true);
    filaments.save_current_preset("PMMA 0.6 Diamondback");
    const Preset *overwritten = filaments.find_preset("PMMA 0.6 Diamondback");
    REQUIRE(overwritten != nullptr);
    CHECK(overwritten->config.opt_string("filament_type", 0u) == "PCTG");
    CHECK(binding(*overwritten, "filamentdb_id") == "6a9caaba3379b2e75836422e");
    CHECK(binding(*overwritten, "filamentdb_nozzle") == "0.6 Diamondback");
}

TEST_CASE("slicer_printer_identity walks every inherited preset", "[FilamentDB]")
{
    TempDataDir  data;
    AppConfig    app{AppConfig::EAppMode::Editor};
    PresetBundle bundle;
    bundle.setup_directories();
    // A hand-edited chain through user presets.
    write_file(data.dir() / "printer" / "My INDX.ini", "inherits = INDX base\nnozzle_diameter = 0.4\n");
    write_file(data.dir() / "printer" / "INDX base.ini", "inherits = Prusa Core One INDX\nnozzle_diameter = 0.4\n");
    write_file(data.dir() / "printer" / "Prusa Core One INDX.ini", "nozzle_diameter = 0.4\n");
    app.set("filamentdb_url", "");
    app.set("presets", "printer", "My INDX");
    bundle.load_presets(app, ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
    REQUIRE(bundle.printers.get_edited_preset().name == "My INDX");

    const SlicerPrinterIdentity who = slicer_printer_identity(bundle);
    CHECK(who.physical_printer.empty());
    CHECK(who.preset_names == std::vector<std::string>{ "My INDX", "INDX base", "Prusa Core One INDX" });

    // ... so the Filament DB printer named like the far end of the chain resolves.
    const std::vector<FilamentDBPrinter> printers = { { "p1", "Prusa Core One INDX", {} } };
    REQUIRE(filamentdb_detail::resolve_printer(printers, who) != nullptr);
}

TEST_CASE("A project's filamentdb_nozzle reaches an installed preset that lacks it", "[FilamentDB]")
{
    TempDataDir  data;
    AppConfig    app{AppConfig::EAppMode::Editor};
    PresetBundle bundle;
    bundle.setup_directories();
    // Saved before the hint was kept: the id, but no hint.
    write_file(data.dir() / "filament" / "PCTG 0.4 Other HF.ini",
               "filament_type = PCTG\nfilamentdb_id = 6a1a7bef677d648e9ba9cd26\n");
    app.set("filamentdb_url", "");
    app.set("presets", "filament", "PCTG 0.4 Other HF");
    bundle.load_presets(app, ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
    PresetCollection &filaments = bundle.filaments;
    const DynamicPrintConfig installed = filaments.get_selected_preset().config;
    using LoadAndSelect = PresetCollection::LoadAndSelect;

    // Same values, no hint in the project: the installed preset as it is.
    {
        DynamicPrintConfig project = installed;
        ExternalPreset r = filaments.load_external_preset("p.3mf", "p", "PCTG 0.4 Other HF", project, LoadAndSelect::Always);
        REQUIRE(r.preset != nullptr);
        CHECK(r.preset->name == "PCTG 0.4 Other HF");
        CHECK_FALSE(r.is_modified);
    }
    // Same values plus the hint: it is carried onto the preset.
    DynamicPrintConfig project = installed;
    project.opt_string("filamentdb_nozzle", true) = "0.4 Other HF";
    ExternalPreset r = filaments.load_external_preset("p.3mf", "p", "PCTG 0.4 Other HF", project, LoadAndSelect::Always);
    REQUIRE(r.preset != nullptr);
    CHECK(r.preset->name == "PCTG 0.4 Other HF");
    CHECK(r.is_modified);
    CHECK(filamentdb_detail::nozzle_hint(filaments.get_edited_preset().config) == "0.4 Other HF");
    CHECK(binding(filaments.get_edited_preset(), "filamentdb_id") == "6a1a7bef677d648e9ba9cd26");
}

TEST_CASE("The printer cache is per Filament DB URL, ignoring a trailing slash", "[FilamentDB]")
{
    const std::string url = "http://filamentdb.test:3456";
    filamentdb_detail::store_printer_cache(url + "/", audit_printers());

    std::vector<FilamentDBPrinter> printers;
    REQUIRE(cached_filamentdb_printers(url, printers));
    CHECK(printers.size() == 3);
    REQUIRE(cached_filamentdb_printers(url + "//", printers));
    CHECK_FALSE(cached_filamentdb_printers("http://other.test:3456", printers));

    // A lookup against a URL with no cached list reads nothing, and asks nothing.
    FilamentCalibrationLookup lookup;
    lookup.filament_name   = "Push Plastic PMMA 0.4 Other HF";
    lookup.nozzle_hint     = "0.4 Other HF";
    lookup.nozzle_diameter = 0.4;
    lookup.high_flow       = true;
    CHECK_FALSE(lookup_filament_calibration("http://other.test:3456", lookup).found);
}
