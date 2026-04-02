///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer Calibration Tools
///|/
#include "FilamentDB.hpp"
#include "Http.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <boost/log/trivial.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <sstream>

namespace Slic3r {

std::vector<FilamentDBPreset> parse_filamentdb_bundle(const std::string &ini_content)
{
    std::vector<FilamentDBPreset> presets;
    std::istringstream stream(ini_content);
    std::string line;

    FilamentDBPreset *current = nullptr;

    while (std::getline(stream, line)) {
        boost::algorithm::trim(line);

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#')
            continue;

        // Check for section header [filament:Name]
        if (line.front() == '[' && line.back() == ']') {
            std::string section = line.substr(1, line.size() - 2);
            const std::string prefix = "filament:";

            if (section.compare(0, prefix.size(), prefix) == 0) {
                std::string name = section.substr(prefix.size());
                boost::algorithm::trim(name);

                // Skip internal/abstract presets (PrusaSlicer convention)
                if (!name.empty() && name.front() == '*' && name.back() == '*') {
                    current = nullptr;
                    continue;
                }

                presets.emplace_back();
                current = &presets.back();
                current->name = name;
            } else {
                // Non-filament section — stop tracking
                current = nullptr;
            }
            continue;
        }

        // Parse key = value pairs
        if (current) {
            auto eq_pos = line.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = line.substr(0, eq_pos);
                std::string value = line.substr(eq_pos + 1);
                boost::algorithm::trim(key);
                boost::algorithm::trim(value);

                // Extract vendor and type for metadata
                if (key == "filament_vendor")
                    current->vendor = value;
                else if (key == "filament_type")
                    current->type = value;

                current->config_pairs.emplace_back(std::move(key), std::move(value));
            }
        }
    }

    return presets;
}

int load_filaments_from_filamentdb(
    PresetBundle &preset_bundle,
    const std::string &api_url,
    std::string &error_message)
{
    // Build the endpoint URL
    std::string url = api_url;
    // Ensure trailing slash
    if (!url.empty() && url.back() != '/')
        url += '/';
    url += "api/filaments/prusaslicer";

    BOOST_LOG_TRIVIAL(info) << "FilamentDB: Fetching presets from " << url;

    std::string response_body;
    bool success = false;

    auto http = Http::get(std::move(url));

    http.on_error([&](std::string body, std::string error, unsigned status) {
        BOOST_LOG_TRIVIAL(warning) << "FilamentDB: HTTP error: " << error
                                   << " (status " << status << ")";
        error_message = "FilamentDB connection failed: " + error;
        if (status > 0)
            error_message += " (HTTP " + std::to_string(status) + ")";
    })
    .on_complete([&](std::string body, unsigned status) {
        if (status != 200) {
            error_message = "FilamentDB returned HTTP " + std::to_string(status);
            BOOST_LOG_TRIVIAL(warning) << error_message;
            return;
        }
        response_body = std::move(body);
        success = true;
    })
    .perform_sync();

    if (!success)
        return -1;

    // Parse the INI bundle
    auto remote_presets = parse_filamentdb_bundle(response_body);

    if (remote_presets.empty()) {
        BOOST_LOG_TRIVIAL(info) << "FilamentDB: No filament presets found in response";
        return 0;
    }

    BOOST_LOG_TRIVIAL(info) << "FilamentDB: Parsed " << remote_presets.size() << " presets";

    int loaded = 0;

    for (const auto &rp : remote_presets) {
        try {
            // Start with default filament config
            DynamicPrintConfig config;
            config.apply(FullPrintConfig::defaults());

            // Apply all key-value pairs from the remote preset
            ConfigSubstitutionContext substitution_ctx(ForwardCompatibilitySubstitutionRule::Enable);
            for (const auto &[key, value] : rp.config_pairs) {
                try {
                    config.set_deserialize(key, value, substitution_ctx);
                } catch (const std::exception &) {
                    // Skip unknown or invalid keys — the remote may have
                    // keys from a newer PrusaSlicer version
                    BOOST_LOG_TRIVIAL(trace) << "FilamentDB: Skipping unknown key '"
                                             << key << "' for preset '" << rp.name << "'";
                }
            }

            // Normalize the config (e.g. set extruder to 0 for single-extruder)
            Preset::normalize(config);

            // Load the preset into the filament collection.
            // Pass config by const-ref so the overload that merges with
            // default_preset().config is used — this ensures all required
            // keys exist and prevents null-dereference in the filament tab UI.
            preset_bundle.filaments.load_preset(
                "",
                rp.name,
                config,
                false  // don't select
            );

            BOOST_LOG_TRIVIAL(debug) << "FilamentDB: Loaded preset '" << rp.name << "'";
            ++loaded;

        } catch (const std::exception &ex) {
            BOOST_LOG_TRIVIAL(warning) << "FilamentDB: Failed to load preset '"
                                       << rp.name << "': " << ex.what();
        }
    }

    BOOST_LOG_TRIVIAL(info) << "FilamentDB: Successfully loaded " << loaded << " presets";
    return loaded;
}

} // namespace Slic3r
