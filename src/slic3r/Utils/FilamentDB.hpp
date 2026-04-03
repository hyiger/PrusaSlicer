///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer Calibration Tools
///|/
#ifndef slic3r_Utils_FilamentDB_hpp_
#define slic3r_Utils_FilamentDB_hpp_

#include <string>
#include <vector>
#include <functional>

namespace Slic3r {

class DynamicPrintConfig;
class PresetBundle;

// Represents a single filament preset fetched from a Filament DB REST API.
struct FilamentDBPreset {
    std::string name;
    std::string vendor;
    std::string type;
    // All key-value pairs from the INI section, ready to load into DynamicPrintConfig.
    std::vector<std::pair<std::string, std::string>> config_pairs;
};

// Fetch filament presets from a Filament DB REST API and load them into the
// PresetBundle's filament collection.
//
// The API endpoint is expected to return a PrusaSlicer-compatible INI config
// bundle with [filament:Name] sections (GET /api/filaments/prusaslicer).
//
// Presets loaded from the remote are marked as user presets and saved to the
// local filament preset directory so they persist across restarts. A fresh
// fetch replaces the previously synced presets.
//
// Returns the number of presets loaded, or -1 on error.
int load_filaments_from_filamentdb(
    PresetBundle &preset_bundle,
    const std::string &api_url,
    std::string &error_message
);

// Parse a PrusaSlicer INI config bundle string into FilamentDBPreset structs.
// This is a standalone function for testability.
std::vector<FilamentDBPreset> parse_filamentdb_bundle(const std::string &ini_content);

// Sync a filament preset back to the FilamentDB server.
// Serializes the config to JSON and PUTs to /api/filaments/{name}.
// Returns true on success, false on error (error_message is set).
// Non-fatal — errors are logged but don't block the local save.
bool sync_filament_to_filamentdb(
    const std::string &api_url,
    const std::string &preset_name,
    const DynamicPrintConfig &config,
    std::string &error_message
);

} // namespace Slic3r

#endif // slic3r_Utils_FilamentDB_hpp_
