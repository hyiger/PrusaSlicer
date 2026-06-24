///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationPAPostProcessor.hpp"

#include "libslic3r/Exception.hpp"
#include "libslic3r/PrintConfig.hpp"   // GCodeFlavor
#include "libslic3r/format.hpp"

#include <LocalesUtils.hpp>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include <algorithm>
#include <cstdio>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace Slic3r {

static constexpr const char *BUILTIN_PREFIX = "::builtin::pa_calibration";

bool is_calibration_pa_url(const std::string &script)
{
    return boost::starts_with(script, BUILTIN_PREFIX);
}

PACalibrationCommand select_pa_command(GCodeFlavor flavor, const std::string &printer_model)
{
    if (flavor == gcfKlipper)
        return PACalibrationCommand::Klipper;
    // Non-Marlin flavors that take a pressure-advance command (RepRapFirmware / Duet).
    if (flavor != gcfMarlinFirmware && flavor != gcfMarlinLegacy)
        return PACalibrationCommand::M572;
    // Marlin-flavored, which includes ALL Prusa printers. Prusa's Buddy input-shaper
    // generation uses M572 (pressure advance); older firmware and generic Marlin use
    // M900 K (linear advance). Mirror Prusa's own model split from start_filament_gcode:
    // M572 for COREONE, MK3.5, MK3.9S, MK4S, MK4IS, MINI-IS and XL*IS variants.
    std::string m = printer_model;
    std::transform(m.begin(), m.end(), m.begin(), ::toupper);
    static const std::regex m572_re(R"(COREONE|MK3\.5|MK3\.9S|MK4S|MK4IS|MINIIS|XL\w*IS)");
    return std::regex_search(m, m572_re) ? PACalibrationCommand::M572
                                         : PACalibrationCommand::M900;
}

namespace {

const char *cmd_token(PACalibrationCommand cmd)
{
    switch (cmd) {
    case PACalibrationCommand::Klipper: return "klipper";
    case PACalibrationCommand::M900:    return "m900";
    case PACalibrationCommand::M572:
    default:                            return "m572";
    }
}

PACalibrationCommand cmd_from_token(const std::string &t)
{
    if (t == "klipper") return PACalibrationCommand::Klipper;
    if (t == "m900")    return PACalibrationCommand::M900;
    return PACalibrationCommand::M572;
}

// Firmware pressure-advance command for a value (locale-invariant).
std::string pa_command(PACalibrationCommand cmd, double pa)
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%.4f", pa);
    switch (cmd) {
    case PACalibrationCommand::Klipper: return std::string("SET_PRESSURE_ADVANCE ADVANCE=") + buf + "\n";
    case PACalibrationCommand::M900:    return std::string("M900 K") + buf + "\n";
    case PACalibrationCommand::M572:
    default:                            return std::string("M572 S") + buf + "\n";
    }
}

// Klipper sanitizes object-label names (see LabelObjects.cpp). Mirror it so the
// PA map matches the rewritten names if a Klipper EXCLUDE_OBJECT form is used.
std::string klipper_sanitize(const std::string &name)
{
    static const std::string banned = "\b\t\n\v\f\r \"#%&\'*-./:;<>\\";
    std::string out = name;
    std::replace_if(out.begin(), out.end(),
                    [](char c) { return banned.find(c) != std::string::npos; }, '_');
    return out;
}

struct PAParams
{
    PACalibrationCommand        cmd  = PACalibrationCommand::M572;
    double                      base = 0.0;
    std::map<std::string, double> pa; // object name -> PA
};

// Throws on a malformed URL.
PAParams parse_url(const std::string &url)
{
    auto qpos = url.find('?');
    if (qpos == std::string::npos)
        throw Slic3r::RuntimeError(format("pa_calibration URL has no query string: %1%", url));

    PAParams p;
    bool     have_objects = false;

    std::string query = url.substr(qpos + 1);
    size_t      pos   = 0;
    while (pos < query.size()) {
        size_t      amp = query.find('&', pos);
        std::string kv  = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        pos             = amp == std::string::npos ? query.size() : amp + 1;

        size_t eq = kv.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = kv.substr(0, eq);
        std::string val = kv.substr(eq + 1);

        if (key == "cmd") {
            p.cmd = cmd_from_token(val);
        } else if (key == "base") {
            p.base = std::stod(val);
        } else if (key == "objects") {
            size_t lp = 0;
            while (lp < val.size()) {
                size_t      comma = val.find(',', lp);
                std::string pair  = val.substr(lp, comma == std::string::npos ? std::string::npos : comma - lp);
                lp                = comma == std::string::npos ? val.size() : comma + 1;
                // Split at the LAST ':' so a name may itself contain ':'.
                size_t colon = pair.rfind(':');
                if (colon == std::string::npos)
                    throw Slic3r::RuntimeError(format("pa_calibration object missing ':': %1%", pair));
                std::string name = pair.substr(0, colon);
                double      pa   = std::stod(pair.substr(colon + 1));
                p.pa[name]       = pa;
                p.pa.emplace(klipper_sanitize(name), pa);
            }
            have_objects = true;
        }
    }

    if (!have_objects || p.pa.empty())
        throw Slic3r::RuntimeError(format("pa_calibration URL missing objects: %1%", url));
    return p;
}

// OctoPrint object-label comments carry the name plus a trailing
// " id:<n> copy <m>" suffix (LabelObjects::init). Recover the bare name.
std::string octoprint_object_name(const std::string &after_prefix)
{
    std::string name = after_prefix;
    boost::trim(name);
    size_t id = name.rfind(" id:");
    if (id != std::string::npos && name.find(" copy ", id) != std::string::npos)
        name = name.substr(0, id);
    return name;
}

} // namespace

std::string make_calibration_pa_url(PACalibrationCommand cmd, double base_pa,
                                    const std::vector<std::pair<std::string, double>> &objects)
{
    // Force the C numeric locale so the URL always uses '.' decimals; it is
    // comma-delimited, so a comma decimal separator would make it ambiguous.
    CNumericLocalesSetter locales_setter;

    std::ostringstream out;
    out << BUILTIN_PREFIX << "?cmd=" << cmd_token(cmd) << "&base=";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4f", base_pa);
    out << buf << "&objects=";
    for (size_t i = 0; i < objects.size(); ++i) {
        if (i != 0) out << ',';
        out << objects[i].first << ':';
        std::snprintf(buf, sizeof(buf), "%.4f", objects[i].second);
        out << buf;
    }
    return out.str();
}

bool run_calibration_pa_post_processor(const std::string &url, const std::string &gcode_path)
{
    BOOST_LOG_TRIVIAL(info) << "pa_calibration: starting in-process post-process on " << gcode_path;

    CNumericLocalesSetter locales_setter;

    const PAParams params = parse_url(url);

    boost::filesystem::path src(gcode_path);

    // Pass 1 — only a G-code carrying the job-scoped calibration marker should
    // be rewritten; the post_process hook persists in the preset and runs for
    // every slice, so a normal print must pass through untouched.
    {
        boost::nowide::ifstream scan(gcode_path);
        if (!scan.is_open())
            throw Slic3r::RuntimeError(format("pa_calibration: cannot open %1%", gcode_path));
        const std::string marker = calibration_pa_marker();
        bool              found  = false;
        std::string       l;
        while (std::getline(scan, l)) {
            if (l.find(marker) != std::string::npos) { found = true; break; }
        }
        if (!found) {
            BOOST_LOG_TRIVIAL(info) << "pa_calibration: marker absent, leaving " << gcode_path << " unchanged";
            return false;
        }
    }

    // A marker-carrying file should be ASCII (the dialog forces binary_gcode=false).
    {
        boost::nowide::ifstream sniff(gcode_path, std::ios::binary);
        char magic[4] = {};
        sniff.read(magic, 4);
        if (sniff.gcount() == 4 && std::string(magic, 4) == "GCDE")
            throw Slic3r::RuntimeError(format(
                "pa_calibration: input %1% is binary G-code (.bgcode); the in-process "
                "rewriter only operates on ASCII. Set binary_gcode=false for calibration prints.",
                gcode_path));
    }

    boost::filesystem::path tmp = src;
    tmp += ".pacal.tmp";

    boost::nowide::ifstream in(gcode_path);
    if (!in.is_open())
        throw Slic3r::RuntimeError(format("pa_calibration: cannot open %1%", gcode_path));
    boost::nowide::ofstream out(tmp.string(), std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        throw Slic3r::RuntimeError(format("pa_calibration: cannot write %1%", tmp.string()));

    auto pa_for_name = [&](const std::string &name) -> double {
        auto it = params.pa.find(name);
        return it == params.pa.end() ? params.base : it->second;
    };

    static const std::string kStart = "; printing object ";
    static const std::string kStop  = "; stop printing object ";
    const std::string        marker = calibration_pa_marker();

    // OctoPrint labeling emits an all-objects header (LabelObjects::all_objects_header)
    // that lists every object with the SAME "; printing object" / "; stop printing
    // object" comments BEFORE the start G-code. Only inject once the first-layer
    // marker has been seen, so PA commands land at real band boundaries, never in
    // that header (before homing / tool setup).
    bool        started  = false;
    size_t      injected = 0;
    std::string line;
    line.reserve(256);
    while (std::getline(in, line)) {
        std::string raw = line;
        if (!raw.empty() && raw.back() == '\r')
            raw.pop_back();

        if (!started && raw.find(marker) != std::string::npos)
            started = true;

        if (started && boost::starts_with(raw, kStart)) {
            out << line << '\n';
            out << pa_command(params.cmd, pa_for_name(octoprint_object_name(raw.substr(kStart.size()))));
            ++injected;
            continue;
        }
        if (started && boost::starts_with(raw, kStop)) {
            out << line << '\n';
            out << pa_command(params.cmd, params.base);   // restore baseline between bands
            continue;
        }
        out << line << '\n';
    }
    in.close();
    out.close();

    boost::system::error_code ec;
    boost::filesystem::rename(tmp, src, ec);
    if (ec) {
        // Fallback: rename can fail across filesystems, or (Windows) when the
        // destination already exists; copy over it and drop the temp file.
        boost::filesystem::copy_file(tmp, src, boost::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
            throw Slic3r::RuntimeError(format("pa_calibration: failed replacing %1%: %2%",
                                              src.string(), ec.message()));
        boost::filesystem::remove(tmp, ec);
    }
    BOOST_LOG_TRIVIAL(info) << "pa_calibration: injected PA at " << injected << " object boundaries in " << gcode_path;
    return true;
}

} // namespace Slic3r
