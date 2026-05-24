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
#include <boost/nowide/fstream.hpp>
#include <boost/filesystem.hpp>
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

FilamentCalibration fetch_filament_calibration(
    const std::string &api_url,
    const std::string &filament_name,
    double nozzle_diameter)
{
    FilamentCalibration result;

    std::string url = api_url;
    if (!url.empty() && url.back() != '/')
        url += '/';
    url += "api/filaments/" + Http::url_encode(filament_name)
         + "/calibration?nozzle_diameter=" + std::to_string(nozzle_diameter);

    BOOST_LOG_TRIVIAL(info) << "FilamentDB: Fetching calibration for '"
                            << filament_name << "' at " << nozzle_diameter << "mm";

    std::string response_body;
    auto http = Http::get(std::move(url));
    http.on_complete([&](std::string body, unsigned status) {
            if (status == 200) {
                response_body = std::move(body);
                result.found = true;
            } else {
                BOOST_LOG_TRIVIAL(debug) << "FilamentDB: No calibration (HTTP " << status << ")";
            }
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(debug) << "FilamentDB: Calibration fetch failed: " << error;
        })
        .perform_sync();

    if (!result.found || response_body.empty())
        return result;

    // Simple JSON value extraction (avoids adding a JSON library dependency)
    auto extract_double = [&](const std::string &key) -> double {
        std::string search = "\"" + key + "\":";
        auto pos = response_body.find(search);
        if (pos == std::string::npos) return -1;
        pos += search.size();
        // Skip whitespace
        while (pos < response_body.size() && (response_body[pos] == ' ' || response_body[pos] == '\t'))
            ++pos;
        if (pos >= response_body.size() || response_body.substr(pos, 4) == "null")
            return -1;
        try { return std::stod(response_body.substr(pos)); }
        catch (...) { return -1; }
    };

    result.pressure_advance    = extract_double("pressureAdvance");
    result.max_volumetric_speed = extract_double("maxVolumetricSpeed");
    result.extrusion_multiplier = extract_double("extrusionMultiplier");
    result.retract_length      = extract_double("retractLength");
    result.retract_speed       = extract_double("retractSpeed");
    result.retract_lift        = extract_double("retractLift");

    BOOST_LOG_TRIVIAL(info) << "FilamentDB: Calibration for '" << filament_name
                            << "' @ " << nozzle_diameter << "mm:"
                            << " PA=" << result.pressure_advance
                            << " MVS=" << result.max_volumetric_speed
                            << " EM=" << result.extrusion_multiplier;
    return result;
}

// Escape a string for JSON (handles quotes, backslashes, control chars)
static std::string json_escape(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += c;      break;
        }
    }
    return out;
}

// Internal helper: do a single HTTP request and capture the status/body/error.
struct HttpAttempt {
    int         status = 0;
    std::string body;
    std::string transport_error; // non-empty if the request didn't even complete
    bool        ok() const { return status >= 200 && status < 300; }
};

static HttpAttempt http_post_json(const std::string &url, const std::string &json)
{
    HttpAttempt r;
    auto http = Http::post(url);
    http.header("Content-Type", "application/json")
        .set_post_body(json)
        .on_complete([&](std::string body, unsigned status) {
            r.status = static_cast<int>(status);
            r.body = std::move(body);
        })
        .on_error([&](std::string body, std::string err, unsigned status) {
            r.status = static_cast<int>(status);
            r.body = std::move(body);
            r.transport_error = std::move(err);
        })
        .perform_sync();
    BOOST_LOG_TRIVIAL(debug) << "FilamentDB POST " << url
                             << " -> " << r.status
                             << (r.transport_error.empty() ? "" : (" transport=" + r.transport_error));
    return r;
}

namespace filamentdb_detail {

std::string config_first_string(const DynamicPrintConfig &cfg, const char *key)
{
    const ConfigOption *opt = cfg.option(key);
    if (opt == nullptr)
        return {};
    if (auto *s = dynamic_cast<const ConfigOptionString *>(opt))
        return s->value;
    if (auto *ss = dynamic_cast<const ConfigOptionStrings *>(opt))
        return ss->values.empty() ? std::string() : ss->values.front();
    return {};
}

} // namespace filamentdb_detail

FilamentDBSyncResult sync_filament_to_filamentdb_detailed(
    const std::string &api_url,
    const std::string &preset_name,
    const DynamicPrintConfig &config,
    double nozzle_diameter,
    bool high_flow)
{
    FilamentDBSyncResult result;

    if (api_url.empty()) {
        result.error_message = "FilamentDB URL is not configured (Preferences → filamentdb_url)";
        return result;
    }

    // Build base URL and the per-name endpoint with nozzle query params.
    std::string base = api_url;
    if (!base.empty() && base.back() == '/')
        base.pop_back();
    const std::string name_url_unqueried = base + "/api/filaments/" + Http::url_encode(preset_name);
    const std::string query = (nozzle_diameter > 0)
        ? "?nozzle_diameter=" + std::to_string(nozzle_diameter)
            + "&high_flow=" + (high_flow ? "1" : "0")
        : std::string();
    const std::string name_url = name_url_unqueried + query;

    // Build update body: {"name": "...", "config": {...}}
    std::string update_body = "{\"name\":\"" + json_escape(preset_name) + "\",\"config\":{";
    bool first = true;
    for (const std::string &key : config.keys()) {
        if (!first) update_body += ',';
        update_body += "\"" + json_escape(key) + "\":\"" + json_escape(config.opt_serialize(key)) + "\"";
        first = false;
    }
    update_body += "}}";

    BOOST_LOG_TRIVIAL(info) << "FilamentDB: Syncing preset '" << preset_name << "' (POST " << name_url << ")";

    // Attempt 1: POST /api/filaments/{name} — canonical update path.
    HttpAttempt a1 = http_post_json(name_url, update_body);
    result.method = "POST";
    result.http_status = a1.status;

    if (a1.ok()) {
        result.success = true;
        result.created_new = (a1.status == 201);
        result.existed_before = !result.created_new;
        BOOST_LOG_TRIVIAL(info) << "FilamentDB: Synced '" << preset_name << "' (HTTP " << a1.status << ")";
        return result;
    }

    // 404 from the per-name route means "filament not found" → switch to create.
    // Other failures (500, network errors) are reported as-is without retry.
    if (a1.status != 404 && a1.status != 405 && a1.status != 501) {
        if (!a1.transport_error.empty())
            result.error_message = a1.transport_error;
        else
            result.error_message = "HTTP " + std::to_string(a1.status)
                                 + (a1.body.empty() ? "" : (" — " + a1.body.substr(0, 200)));
        BOOST_LOG_TRIVIAL(warning) << result.error_message;
        return result;
    }

    // Attempt 2: collection create — POST /api/filaments with {name, type, vendor, config}.
    // Server requires `type` and `vendor`; everything else is optional and stored
    // server-side (see Filament model). We extract from the preset's config.
    std::string filament_type   = filamentdb_detail::config_first_string(config, "filament_type");
    std::string filament_vendor = filamentdb_detail::config_first_string(config, "filament_vendor");
    if (filament_type.empty())   filament_type   = "PLA";
    if (filament_vendor.empty()) filament_vendor = "User";

    std::string create_body = "{"
        "\"name\":\""   + json_escape(preset_name)    + "\","
        "\"type\":\""   + json_escape(filament_type)  + "\","
        "\"vendor\":\"" + json_escape(filament_vendor) + "\""
        "}";

    const std::string collection_url = base + "/api/filaments";
    BOOST_LOG_TRIVIAL(info) << "FilamentDB: '" << preset_name << "' missing — creating via POST " << collection_url;

    HttpAttempt a2 = http_post_json(collection_url, create_body);
    if (!a2.ok()) {
        result.method = "POST collection";
        result.http_status = a2.status;
        if (!a2.transport_error.empty())
            result.error_message = "Create failed: " + a2.transport_error;
        else
            result.error_message = "Create failed: HTTP " + std::to_string(a2.status)
                                 + (a2.body.empty() ? "" : (" — " + a2.body.substr(0, 200)));
        BOOST_LOG_TRIVIAL(warning) << result.error_message;
        return result;
    }

    // Attempt 3: now that the entry exists, retry the per-name update so the
    // server populates calibrations / per-nozzle data. This is the "second
    // half" of the upsert: create gave us metadata, update gives us the
    // calibration body that the create endpoint silently ignored.
    BOOST_LOG_TRIVIAL(info) << "FilamentDB: shell created (HTTP " << a2.status << "); retrying update";
    HttpAttempt a3 = http_post_json(name_url, update_body);
    result.method = "POST (after create)";
    result.http_status = a3.status;
    if (!a3.ok()) {
        // Create did succeed — the calibration push didn't. Treat as soft-success
        // but surface the partial state.
        result.success = true;
        result.created_new = true;
        result.existed_before = false;
        result.error_message = "Created '" + preset_name + "' but calibration update returned HTTP "
                             + std::to_string(a3.status);
        BOOST_LOG_TRIVIAL(warning) << result.error_message;
        return result;
    }

    result.success = true;
    result.created_new = true;
    result.existed_before = false;
    BOOST_LOG_TRIVIAL(info) << "FilamentDB: Created and populated '" << preset_name
                            << "' (create=" << a2.status << ", update=" << a3.status << ")";
    return result;
}

bool sync_filament_to_filamentdb(
    const std::string &api_url,
    const std::string &preset_name,
    const DynamicPrintConfig &config,
    std::string &error_message,
    double nozzle_diameter,
    bool high_flow)
{
    auto r = sync_filament_to_filamentdb_detailed(api_url, preset_name, config,
                                                   nozzle_diameter, high_flow);
    error_message = r.error_message;
    return r.success;
}

SpoolCheckResult check_filament_spool(
    const std::string &api_url,
    const std::string &filament_name,
    double weight_g)
{
    SpoolCheckResult result;
    result.filament_name = filament_name;
    result.required_weight_g = weight_g;

    if (weight_g <= 0 || filament_name.empty() || api_url.empty())
        return result; // ok=true, has_data=false

    std::string url = api_url;
    if (!url.empty() && url.back() != '/')
        url += '/';
    url += "api/filaments/" + Http::url_encode(filament_name)
         + "/spool-check?weight=" + std::to_string(weight_g);

    BOOST_LOG_TRIVIAL(info) << "FilamentDB: Spool check for '" << filament_name
                            << "' requiring " << weight_g << "g";

    std::string response_body;
    auto http = Http::get(std::move(url));
    http.on_complete([&](std::string body, unsigned status) {
            if (status == 200) {
                response_body = std::move(body);
            } else {
                BOOST_LOG_TRIVIAL(debug) << "FilamentDB: Spool check returned HTTP " << status;
            }
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(debug) << "FilamentDB: Spool check failed: " << error;
        })
        .perform_sync();

    if (response_body.empty())
        return result; // ok=true, has_data=false — can't reach server, don't block

    // Simple JSON parsing (no library dependency)
    // Check "ok":true/false
    auto pos_ok = response_body.find("\"ok\":");
    if (pos_ok != std::string::npos) {
        auto val_start = pos_ok + 5;
        while (val_start < response_body.size() && response_body[val_start] == ' ')
            ++val_start;
        result.ok = response_body.substr(val_start, 4) == "true";
        result.has_data = true;
    }

    // Extract "warning":"..." — scan for the closing quote while honoring
    // JSON escapes so an embedded \" (e.g. a quoted spool label inside the
    // message) doesn't truncate the text. Decode the common escapes inline
    // so the user-facing string renders cleanly.
    auto pos_warn = response_body.find("\"warning\":\"");
    if (pos_warn != std::string::npos) {
        std::size_t i = pos_warn + 11;
        std::string decoded;
        decoded.reserve(64);
        bool terminated = false;
        while (i < response_body.size()) {
            char c = response_body[i];
            if (c == '"') { terminated = true; break; }
            if (c == '\\' && i + 1 < response_body.size()) {
                char esc = response_body[i + 1];
                switch (esc) {
                    case '"':  decoded += '"';  break;
                    case '\\': decoded += '\\'; break;
                    case '/':  decoded += '/';  break;
                    case 'n':  decoded += '\n'; break;
                    case 't':  decoded += '\t'; break;
                    case 'r':  decoded += '\r'; break;
                    case 'b':  decoded += '\b'; break;
                    case 'f':  decoded += '\f'; break;
                    default:   decoded += esc;  break; // \uXXXX etc. — drop the backslash
                }
                i += 2;
                continue;
            }
            decoded += c;
            ++i;
        }
        if (terminated)
            result.warning = std::move(decoded);
    }

    // If no spools array or "message" about no data, treat as no data
    if (response_body.find("\"spools\":[]") != std::string::npos)
        result.has_data = false;

    BOOST_LOG_TRIVIAL(info) << "FilamentDB: Spool check result: ok=" << result.ok
                            << " has_data=" << result.has_data
                            << (result.warning.empty() ? "" : " warning=" + result.warning);
    return result;
}

} // namespace Slic3r
