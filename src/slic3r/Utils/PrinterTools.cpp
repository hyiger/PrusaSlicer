///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "PrinterTools.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <regex>
#include <sstream>

#include <memory>

#include <nlohmann/json.hpp>

#include "libslic3r/Preset.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "PrintHost.hpp"

namespace Slic3r {

namespace {

// A config that made its options from their definitions (new_from_defaults_keys()) holds
// generic enums, which PrintHost and PrusaLink can't cast to the typed enums they read.
template<class T> void make_enum_typed(DynamicPrintConfig &config, const std::string &key)
{
    const ConfigOption *opt = config.option(key);
    if (opt && opt->type() == coEnum && !dynamic_cast<const ConfigOptionEnum<T> *>(opt))
        config.set_key_value(key, new ConfigOptionEnum<T>(T(opt->getInt())));
}

} // namespace

bool fetch_printer_tools(const DynamicPrintConfig &physical_printer_config, PrinterToolsResult &out, const PrinterToolsOptions &opts)
{
    DynamicPrintConfig config = physical_printer_config;
    make_enum_typed<PrinterTechnology>(config, "printer_technology");
    make_enum_typed<PrintHostType>(config, "host_type");
    make_enum_typed<AuthorizationType>(config, "printhost_authorization_type");
    // The hosts read their keys without checking that they exist
    for (const std::string &key : PhysicalPrinter::printer_options())
        if (!config.has(key))
            if (const ConfigOptionDef *def = print_config_def.get(key); def && def->default_value)
                config.set_key_value(key, def->default_value->clone());
    const std::unique_ptr<PrintHost> host(PrintHost::get_print_host(&config));
    if (!host) {
        out        = {};
        out.status = PrinterToolsStatus::Unsupported;
        return false;
    }
    return host->get_tools(out, opts);
}

namespace printer_tools_detail {

namespace {

using json = nlohmann::json;

// The firmware numbers tools "1".."8"; three digits leave room without risking overflow.
bool parse_tool_key(const std::string &key, int &number)
{
    if (key.empty() || key.size() > 3 || key[0] == '0')
        return false;
    const auto [ptr, ec] = std::from_chars(key.data(), key.data() + key.size(), number);
    return ec == std::errc{} && ptr == key.data() + key.size() && number >= 1;
}

std::string trim(const std::string &s)
{
    const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    auto b = std::find_if_not(s.begin(), s.end(), is_space);
    auto e = std::find_if_not(s.rbegin(), s.rend(), is_space).base();
    return b < e ? std::string(b, e) : std::string();
}

std::string cap(std::string s)
{
    constexpr size_t max_len = 120;
    if (s.size() > max_len)
        s = s.substr(0, max_len - 3) + "...";
    return s;
}

} // namespace

std::string normalize_color_hex(const std::string &hex)
{
    if (hex.size() != 7 || hex[0] != '#')
        return {};
    std::string out = "#";
    for (size_t i = 1; i < hex.size(); ++i) {
        const unsigned char c = hex[i];
        if (!std::isxdigit(c))
            return {};
        out += char(std::toupper(c));
    }
    return out;
}

bool parse_tools_json(const std::string &body, PrinterTools &out, std::string &error)
{
    out = {};
    try {
        const json root = json::parse(body, nullptr, /*allow_exceptions*/ false);
        if (root.is_discarded()) {
            error = "the reply isn't JSON";
            return false;
        }
        const auto tools_it = root.is_object() ? root.find("tools") : root.end();
        if (!root.is_object() || tools_it == root.end() || !tools_it->is_object()) {
            error = "the reply has no \"tools\" object";
            return false;
        }

        for (const auto &item : tools_it->items()) {
            const std::string &key = item.key();
            const json        &tool = item.value();
            const auto skip = [&](const std::string &why) {
                out.warnings.emplace_back("tool \"" + key + "\": " + why + ", skipped");
            };

            int number = 0;
            if (!parse_tool_key(key, number)) {
                skip("not a tool number");
                continue;
            }
            if (!tool.is_object()) {
                skip("not an object");
                continue;
            }

            PrinterTool t;
            t.extruder_idx = number - 1;

            const auto loaded = tool.find("loaded");
            if (loaded == tool.end() || !loaded->is_boolean()) {
                skip("\"loaded\" missing or not true/false");
                continue;
            }
            t.loaded = loaded->get<bool>();

            const auto diameter = tool.find("nozzle_diameter");
            if (diameter == tool.end() || !diameter->is_number()) {
                skip("\"nozzle_diameter\" missing or not a number");
                continue;
            }
            t.nozzle_diameter = diameter->get<double>();

            bool ok = true;
            for (auto [name, field] : {std::pair<const char *, bool *>{"hardened", &t.hardened}, {"high_flow", &t.high_flow}}) {
                const auto it = tool.find(name);
                if (it == tool.end())
                    continue;
                if (!it->is_boolean()) {
                    skip(std::string("\"") + name + "\" isn't true/false");
                    ok = false;
                    break;
                }
                *field = it->get<bool>();
            }
            if (!ok)
                continue;

            std::string color_hex;
            for (auto [name, field] : {std::pair<const char *, std::string *>{"material", &t.material},
                                       {"vendor", &t.vendor}, {"color", &t.color}, {"color_hex", &color_hex}}) {
                const auto it = tool.find(name);
                if (it == tool.end())
                    continue;
                if (!it->is_string()) {
                    skip(std::string("\"") + name + "\" isn't a string");
                    ok = false;
                    break;
                }
                *field = it->get<std::string>();
            }
            if (!ok)
                continue;

            if (!t.loaded) {
                // An empty tool reports empty names; nothing about it is used.
                t.material.clear();
                t.vendor.clear();
                t.color.clear();
            } else {
                if (t.material.empty()) {
                    skip("loaded but no material");
                    continue;
                }
                t.color_hex = normalize_color_hex(color_hex);
                if (t.color_hex.empty() && !color_hex.empty())
                    out.warnings.emplace_back("tool \"" + key + "\": color \"" + color_hex + "\" isn't #RRGGBB, ignored");
            }
            out.tools.emplace_back(std::move(t));
        }
    } catch (const json::exception &e) {
        out = {};
        error = e.what();
        return false;
    }

    // JSON objects iterate in key order as strings ("10" before "2").
    std::sort(out.tools.begin(), out.tools.end(),
              [](const PrinterTool &a, const PrinterTool &b) { return a.extruder_idx < b.extruder_idx; });
    return true;
}

std::string status_page_summary(const std::string &body, unsigned status)
{
    const json page = json::parse(body, nullptr, /*allow_exceptions*/ false);
    if (page.is_object()) {
        const auto title = page.find("title");
        if (title != page.end() && title->is_string()) {
            std::string s = title->get<std::string>();
            const auto message = page.find("message");
            if (message != page.end() && message->is_string() && !message->get<std::string>().empty())
                s += ": " + message->get<std::string>();
            return cap(s);
        }
    }
    // The firmware cuts status pages at 127 bytes, which can leave the JSON form unterminated.
    static const std::regex title_re(R"re("title"\s*:\s*"([^"]*)")re");
    std::smatch m;
    if (std::regex_search(body, m, title_re))
        return cap(m[1].str());

    std::istringstream lines(body);
    for (std::string line; std::getline(lines, line);) {
        line = trim(line);
        if (!line.empty())
            return cap(line);
    }
    return "HTTP " + std::to_string(status);
}

} // namespace printer_tools_detail
} // namespace Slic3r
