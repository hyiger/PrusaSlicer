///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationPAPostProcessor.hpp"

#include "libslic3r/PrintConfig.hpp"   // GCodeFlavor

#include <cctype>
#include <iomanip>
#include <locale>
#include <regex>
#include <sstream>
#include <string>

namespace Slic3r {

PACalibrationCommand select_pa_command(GCodeFlavor flavor, const std::string &printer_notes)
{
    if (flavor == gcfKlipper)
        return PACalibrationCommand::Klipper;
    // Non-Marlin flavors that take a pressure-advance command (RepRapFirmware / Duet).
    if (flavor != gcfMarlinFirmware && flavor != gcfMarlinLegacy)
        return PACalibrationCommand::M572;
    // Marlin-flavored, which includes ALL Prusa printers. Prusa's Buddy input-shaper
    // firmware uses M572 (pressure advance); older firmware and generic Marlin use
    // M900 K (linear advance). Key off the SAME printer_notes markers Prusa's own
    // start_filament_gcode switches on -- the printer MODEL name is not reliable (e.g.
    // the MK3.9 carries PRINTER_MODEL_MK4IS in its notes, not "MK3.9"). Generic Marlin
    // printers carry none of these markers and correctly fall through to M900.
    static const std::regex m572_re(R"(MK4IS|XLIS|MK4S|MK3\.9S|MK3\.5|MINIIS|COREONE)",
                                    std::regex::icase);
    return std::regex_search(printer_notes, m572_re) ? PACalibrationCommand::M572
                                                     : PACalibrationCommand::M900;
}

const char *pa_command_prefix(PACalibrationCommand cmd)
{
    switch (cmd) {
    case PACalibrationCommand::Klipper: return "SET_PRESSURE_ADVANCE ADVANCE=";
    case PACalibrationCommand::M900:    return "M900 K";
    case PACalibrationCommand::M572:    break;
    }
    return "M572 S";
}

std::string format_pa_command(PACalibrationCommand cmd, double pa)
{
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << pa_command_prefix(cmd) << std::fixed << std::setprecision(4) << pa;
    return s.str();
}

namespace {

// One PlaceholderParser tag "{...}" starting at s[pos] == '{'.
struct TemplateTag
{
    size_t end          = std::string::npos; // one past the closing '}'; npos if unterminated
    int    depth_change = 0;                 // +1 for {if ...}, -1 for {endif}
    bool   branch       = false;             // {elsif ...} or {else}
};

TemplateTag scan_template_tag(const std::string &s, size_t pos)
{
    TemplateTag tag;
    int         braces     = 0;
    bool        first_word = true;
    for (size_t i = pos; i < s.size();) {
        const char c = s[i];
        if (c == '{') {
            ++braces;
            ++i;
        } else if (c == '}') {
            ++i;
            if (--braces == 0) {
                tag.end = i;
                break;
            }
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t j = i;
            while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_'))
                ++j;
            const std::string word = s.substr(i, j - i);
            // Count every if/endif, so that a self-contained "{if a then b; endif}" nets to zero.
            if (word == "if")
                ++tag.depth_change;
            else if (word == "endif")
                --tag.depth_change;
            else if (first_word && (word == "elsif" || word == "else"))
                tag.branch = true;
            first_word = false;
            i = j;
        } else
            ++i;
    }
    return tag;
}

// Whether the command at s[pos] is G-code that runs (in whichever template branch it sits):
// it starts a G-code word (at the line start, after whitespace or after a template tag), and no
// ';' comment precedes it on its line. A ';' inside an {if} block, as in
// "{if MINI};{elsif ...}M900 K200", only comments out its own branch, so it does not count.
bool is_live_command(const std::string &s, size_t pos)
{
    if (pos > 0) {
        const char prev = s[pos - 1];
        if (prev != '\n' && prev != '\r' && prev != ' ' && prev != '\t' && prev != '}')
            return false;
    }
    const size_t nl         = s.rfind('\n', pos);
    const size_t line_start = nl == std::string::npos ? 0 : nl + 1;
    int          depth      = 0;
    bool         commented  = false;
    for (size_t i = line_start; i < pos;) {
        if (s[i] == '{') {
            const TemplateTag tag = scan_template_tag(s, i);
            // The command sits inside a template tag (e.g. a {if ...=~/M572 S/} condition):
            // that is template code, not G-code, and its "value" runs into the closing '}'.
            if (tag.end == std::string::npos || tag.end > pos)
                return false;
            if (depth + tag.depth_change < 0 || (depth == 0 && tag.branch)) {
                // {elsif}/{else}/{endif} of a block opened on an earlier line: a ';' before
                // it on this line belongs to another branch.
                commented = false;
                depth     = 0;
            } else
                depth += tag.depth_change;
            i = tag.end;
        } else {
            if (s[i] == ';' && depth == 0)
                commented = true;
            ++i;
        }
    }
    return !commented;
}

// End of the command value starting at s[pos]: a number, a placeholder or a complete
// {if}...{endif} chain. The value ends at whitespace, a ';' comment or the end of the line, or
// before an {elsif}/{else}/{endif} of an enclosing block. Returns npos when the value is not a
// complete template (e.g. an {if} block spanning lines), which is not safe to replace.
size_t command_value_end(const std::string &s, size_t pos)
{
    int    depth = 0;
    size_t i     = pos;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '{') {
            const TemplateTag tag = scan_template_tag(s, i);
            if (tag.end == std::string::npos)
                return std::string::npos;
            if (depth + tag.depth_change < 0 || (depth == 0 && tag.branch))
                break;
            depth += tag.depth_change;
            i = tag.end;
        } else if (c == '\n' || c == '\r' ||
                   (depth == 0 && (c == ';' || c == ' ' || c == '\t'))) {
            break;
        } else
            ++i;
    }
    return depth == 0 ? i : std::string::npos;
}

} // namespace

std::string apply_pressure_advance_to_start_gcode(const std::string &gcode,
                                                  PACalibrationCommand cmd,
                                                  double pa)
{
    // Builds before this fix appended "\\n" + "M572 S" + std::to_string(pa): a literal
    // backslash-n, so that command never ran. Drop those leftovers from saved presets.
    static const std::regex legacy_append_re(R"(\\nM572 S\d+[.,]\d{6})");
    std::string             out = std::regex_replace(gcode, legacy_append_re, "");

    const std::string prefix  = pa_command_prefix(cmd);
    const std::string command = format_pa_command(cmd, pa);
    const std::string value   = command.substr(prefix.size());

    bool replaced = false;
    for (size_t pos = out.find(prefix); pos != std::string::npos; pos = out.find(prefix, pos)) {
        const size_t value_start = pos + prefix.size();
        const size_t value_end   = is_live_command(out, pos) ? command_value_end(out, value_start)
                                                             : std::string::npos;
        if (value_end == std::string::npos) {
            pos = value_start;
            continue;
        }
        out.replace(value_start, value_end - value_start, value);
        replaced = true;
        pos      = value_start + value.size();
    }

    if (!replaced) {
        if (!out.empty() && out.back() != '\n')
            out += '\n';
        out += command;
    }
    return out;
}

} // namespace Slic3r
