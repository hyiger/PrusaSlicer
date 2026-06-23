///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationPALinePostProcessor.hpp"
#include "CalibrationPAPostProcessor.hpp"   // calibration_pa_marker()

#include "libslic3r/Exception.hpp"
#include "libslic3r/format.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

#include <sstream>
#include <string>

namespace Slic3r {

static constexpr const char *BUILTIN_PREFIX = "::builtin::pa_line_pattern";

bool is_pa_line_url(const std::string &script)
{
    return boost::starts_with(script, BUILTIN_PREFIX);
}

std::string make_pa_line_url(const std::string &body_file)
{
    return std::string(BUILTIN_PREFIX) + "?body=" + body_file;
}

namespace {

// Extract the body= value (throws on a malformed URL).
std::string parse_body_path(const std::string &url)
{
    auto qpos = url.find('?');
    if (qpos == std::string::npos)
        throw Slic3r::RuntimeError(format("pa_line URL has no query string: %1%", url));
    std::string query = url.substr(qpos + 1);
    // Single key=value (body=...); the value (a file path) may contain anything
    // except a leading "key=" — take everything after the first '='.
    size_t eq = query.find('=');
    if (eq == std::string::npos || query.substr(0, eq) != "body")
        throw Slic3r::RuntimeError(format("pa_line URL missing body=: %1%", url));
    std::string path = query.substr(eq + 1);
    if (path.empty())
        throw Slic3r::RuntimeError(format("pa_line URL has empty body path: %1%", url));
    return path;
}

} // namespace

bool run_pa_line_post_processor(const std::string &url, const std::string &gcode_path)
{
    BOOST_LOG_TRIVIAL(info) << "pa_line: starting in-process post-process on " << gcode_path;

    const std::string body_path = parse_body_path(url);
    const std::string marker    = calibration_pa_marker();

    // Pass 1 — only a marker-carrying calibration G-code is rewritten.
    {
        boost::nowide::ifstream scan(gcode_path);
        if (!scan.is_open())
            throw Slic3r::RuntimeError(format("pa_line: cannot open %1%", gcode_path));
        bool        found = false;
        std::string l;
        while (std::getline(scan, l)) {
            if (l.find(marker) != std::string::npos) { found = true; break; }
        }
        if (!found) {
            BOOST_LOG_TRIVIAL(info) << "pa_line: marker absent, leaving " << gcode_path << " unchanged";
            return false;
        }
    }

    // Marker-carrying files must be ASCII (the dialog forces binary_gcode=false).
    {
        boost::nowide::ifstream sniff(gcode_path, std::ios::binary);
        char magic[4] = {};
        sniff.read(magic, 4);
        if (sniff.gcount() == 4 && std::string(magic, 4) == "GCDE")
            throw Slic3r::RuntimeError(format(
                "pa_line: input %1% is binary G-code (.bgcode); the in-process rewriter "
                "only operates on ASCII. Set binary_gcode=false for calibration prints.",
                gcode_path));
    }

    // Load the generated toolpath body.
    std::string body;
    {
        boost::nowide::ifstream bf(body_path, std::ios::binary);
        if (!bf.is_open())
            throw Slic3r::RuntimeError(format(
                "pa_line: cannot read the generated toolpath body %1% — re-run the PA Line "
                "calibration to regenerate it (the body is a temporary file and is not kept "
                "when the project is saved/reopened or the temp directory is cleaned).",
                body_path));
        std::ostringstream ss;
        ss << bf.rdbuf();
        body = ss.str();
    }
    if (body.empty())
        throw Slic3r::RuntimeError(format("pa_line: toolpath body %1% is empty", body_path));

    boost::filesystem::path src(gcode_path);
    boost::filesystem::path tmp = src;
    tmp += ".paline.tmp";

    boost::nowide::ifstream in(gcode_path);
    if (!in.is_open())
        throw Slic3r::RuntimeError(format("pa_line: cannot open %1%", gcode_path));
    boost::nowide::ofstream out(tmp.string(), std::ios::binary | std::ios::trunc);
    if (!out.is_open())
        throw Slic3r::RuntimeError(format("pa_line: cannot write %1%", tmp.string()));

    static const std::string kStart = "; printing object ";
    static const std::string kStop  = "; stop printing object ";

    // Keep header + start G-code up to and including the first post-marker
    // "; printing object" (OctoPrint also lists every object in a header BEFORE
    // the start G-code — gate on the marker so we splice the real body, not that
    // header). Replace the placeholder body with the generated toolpath, then keep
    // the trailing "; stop printing object" + end G-code.
    enum Phase { BEFORE, BODY, AFTER };
    Phase       phase   = BEFORE;
    bool        started = false;
    bool        spliced = false;
    std::string line;
    while (std::getline(in, line)) {
        std::string raw = line;
        if (!raw.empty() && raw.back() == '\r')
            raw.pop_back();

        if (phase == BEFORE) {
            if (!started && raw.find(marker) != std::string::npos)
                started = true;
            out << line << '\n';
            if (started && boost::starts_with(raw, kStart)) {
                out << body;
                if (!body.empty() && body.back() != '\n')
                    out << '\n';
                phase   = BODY;
                spliced = true;
            }
            continue;
        }
        if (phase == BODY) {
            if (boost::starts_with(raw, kStop)) {
                out << line << '\n';
                phase = AFTER;
            }
            // else: drop the placeholder's sliced body
            continue;
        }
        out << line << '\n';   // AFTER: end G-code
    }
    in.close();
    out.close();

    if (!spliced) {
        boost::system::error_code rm;
        boost::filesystem::remove(tmp, rm);
        throw Slic3r::RuntimeError(format(
            "pa_line: no post-marker object body found in %1% to replace "
            "(is OctoPrint object labeling enabled?)", gcode_path));
    }
    if (phase == BODY) {
        // The placeholder body was never closed by "; stop printing object", so the
        // end G-code (cooldown / steppers-off) would have been dropped. Fail loudly
        // rather than write a truncated print.
        boost::system::error_code rm;
        boost::filesystem::remove(tmp, rm);
        throw Slic3r::RuntimeError(format(
            "pa_line: object body in %1% had no closing \"; stop printing object\" — "
            "refusing to silently drop the end G-code", gcode_path));
    }

    boost::system::error_code ec;
    boost::filesystem::rename(tmp, src, ec);
    if (ec) {
        boost::filesystem::copy_file(tmp, src, boost::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
            throw Slic3r::RuntimeError(format("pa_line: failed replacing %1%: %2%",
                                              src.string(), ec.message()));
        boost::filesystem::remove(tmp, ec);
    }
    BOOST_LOG_TRIVIAL(info) << "pa_line: spliced generated toolpath into " << gcode_path;
    return true;
}

} // namespace Slic3r
