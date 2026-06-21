///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationPATestGCode.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace Slic3r {

static constexpr double PA_PI = 3.14159265358979323846;

// Band-spacing geometry, shared between the body builders and the bed-fit
// check so they can never disagree about whether a sweep fits.
static constexpr double PATTERN_AMP        = 3.0;   // zig-zag band thickness (mm)
static constexpr double PATTERN_MIN_GAP    = 1.0;   // min gap between pattern bands (mm)
static constexpr double LINE_MIN_SPACING_K = 1.5;   // × line width: tightest line packing
static constexpr double BED_Y_MARGIN       = 10.0;  // total top+bottom Y margin (mm)

int pa_test_band_count(const PATestParams& p)
{
    if (p.step_pa <= 0.0 || p.end_pa < p.start_pa)
        return 1;
    return static_cast<int>(std::floor((p.end_pa - p.start_pa) / p.step_pa + 1e-9)) + 1;
}

double pa_test_line_width(const PATestParams& p)
{
    return p.line_width > 0.0 ? p.line_width : 1.125 * p.nozzle_diameter;
}

double pa_test_e_per_mm(const PATestParams& p)
{
    // Rounded-rectangle extrudate cross-section, matching Slic3r::Flow:
    //   area = h · (w − h·(1 − π/4))
    const double w    = pa_test_line_width(p);
    const double h    = p.layer_height;
    const double area = h * (w - h * (1.0 - PA_PI / 4.0));
    // Volumetric E: emit mm^3 of material per mm of travel (firmware divides by
    // the filament area). Otherwise emit mm of filament (area / filament area).
    if (p.volumetric_e)
        return area * p.extrusion_multiplier;
    const double fil_area = PA_PI / 4.0 * p.filament_diameter * p.filament_diameter;
    if (fil_area <= 0.0)
        return 0.0;
    return area / fil_area * p.extrusion_multiplier;
}

double pa_test_required_span_y(const PATestParams& p)
{
    const int bands = pa_test_band_count(p);
    if (bands < 2)
        return 0.0;
    if (p.kind == PATestKind::Pattern)
        return (bands - 1) * (PATTERN_AMP + PATTERN_MIN_GAP) + PATTERN_AMP;
    return (bands - 1) * (LINE_MIN_SPACING_K * pa_test_line_width(p));
}

bool pa_test_fits_bed(const PATestParams& p)
{
    const double span_y = std::max(1.0, p.bed_size_y - p.bed_min_y);
    return pa_test_required_span_y(p) <= span_y - BED_Y_MARGIN;
}

namespace {

// Locale-independent fixed-precision number formatting (G-code must always use
// a '.' decimal separator regardless of the UI locale).
std::string num(double v, int prec)
{
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << std::fixed << std::setprecision(prec) << v;
    return s.str();
}

std::string pa_command(const PATestParams& p, double pa)
{
    switch (p.command) {
    case PATestCommand::Klipper: return "SET_PRESSURE_ADVANCE ADVANCE=" + num(pa, 4) + "\n";
    case PATestCommand::M900:    return "M900 K" + num(pa, 4) + "\n";
    case PATestCommand::M572:
    default:                     return "M572 S" + num(pa, 4) + "\n";
    }
}

// Extrusion axis letter, defaulting to "E" when the profile leaves it empty
// (a calibration print still needs an extrusion axis).
std::string axis_of(const PATestParams& p) { return p.extrusion_axis.empty() ? std::string("E") : p.extrusion_axis; }

// Retraction E magnitude — scaled to mm^3 in volumetric mode (× filament area),
// matching GCodeWriter, so retraction matches the deposition units.
double retract_e_of(const PATestParams& p)
{
    if (!p.volumetric_e)
        return p.retract_length;
    return p.retract_length * (PA_PI / 4.0 * p.filament_diameter * p.filament_diameter);
}

// Mutable cursor + emit helpers shared by both body builders.
struct Emitter
{
    std::ostringstream& oss;
    const PATestParams& p;
    double      e_per_mm;
    std::string axis      = axis_of(p);
    double      retract_e = retract_e_of(p);
    double      cur_x = 0.0;
    double      cur_y = 0.0;

    double feed(double mm_s) const { return mm_s * 60.0; }

    void travel(double x, double y)
    {
        oss << "G0 X" << num(x, 3) << " Y" << num(y, 3) << " F" << num(feed(p.travel_speed), 0) << "\n";
        cur_x = x;
        cur_y = y;
    }

    void extrude(double x, double y, double speed)
    {
        const double len = std::hypot(x - cur_x, y - cur_y);
        const double e   = e_per_mm * len;
        oss << "G1 X" << num(x, 3) << " Y" << num(y, 3) << " " << axis << num(e, 5) << " F"
            << num(feed(speed), 0) << "\n";
        cur_x = x;
        cur_y = y;
    }

    void retract()   { oss << "G1 " << axis << "-" << num(retract_e, 5) << " F" << num(feed(40.0), 0) << "\n"; }
    void unretract() { oss << "G1 " << axis << num(retract_e, 5) << " F" << num(feed(40.0), 0) << "\n"; }
};

// Placement of each test kind, computed once and shared by the body builders
// and pa_test_footprint() so geometry and footprint can never disagree.
struct LineLayout    { double x0, y0, total_len, spacing; };
struct PatternLayout { double x_left, x_right, y0, spacing, amp, tooth_w; int teeth; };

LineLayout compute_line_layout(const PATestParams& p, int bands)
{
    const double lw     = pa_test_line_width(p);
    const double span_x = std::max(1.0, p.bed_size_x - p.bed_min_x);
    const double span_y = std::max(1.0, p.bed_size_y - p.bed_min_y);
    // slow / fast / slow in a 1:2:1 ratio, fit to the bed width.
    const double total_len = std::max(10.0, std::min(80.0, span_x - 10.0));
    double spacing = std::max(4.0, 3.0 * lw);
    if (bands > 1) // keep the stack on the bed for large sweeps
        spacing = std::min(spacing, std::max(LINE_MIN_SPACING_K * lw, (span_y - BED_Y_MARGIN) / (bands - 1)));
    const double total_h = (bands - 1) * spacing;
    return { p.bed_min_x + std::max(5.0, (span_x - total_len) / 2.0),
             p.bed_min_y + std::max(5.0, (span_y - total_h)   / 2.0),
             total_len, spacing };
}

PatternLayout compute_pattern_layout(const PATestParams& p, int bands)
{
    const double span_x  = std::max(1.0, p.bed_size_x - p.bed_min_x);
    const double span_y  = std::max(1.0, p.bed_size_y - p.bed_min_y);
    const double tooth_w = 5.0;
    const double amp     = PATTERN_AMP;
    const int    teeth   = std::max(4, static_cast<int>(std::floor(std::min(span_x - 40.0, 80.0) / tooth_w)));
    const double width   = teeth * tooth_w;
    double spacing = amp + 3.0;
    if (bands > 1)
        spacing = std::min(spacing, std::max(amp + PATTERN_MIN_GAP, (span_y - BED_Y_MARGIN - amp) / (bands - 1)));
    const double total_h = (bands - 1) * spacing + amp;
    const double x_left  = p.bed_min_x + std::max(5.0, (span_x - width)   / 2.0);
    return { x_left, x_left + width,
             p.bed_min_y + std::max(5.0, (span_y - total_h) / 2.0),
             spacing, amp, tooth_w, teeth };
}

// One horizontal line per band: slow lead-in, fast middle, slow lead-out. The
// speed change mid-line is what surfaces PA — too high bulges after the speed
// drop, too low gaps. Bands run front (start_pa) to back.
void build_line_body(Emitter& em, const PATestParams& p, int bands)
{
    const LineLayout L     = compute_line_layout(p, bands);
    const double     Lslow = L.total_len * 0.25;
    const double     Lfast = L.total_len * 0.50;

    em.retract(); // park filament before the first travel
    for (int i = 0; i < bands; ++i) {
        const double y  = L.y0 + i * L.spacing;
        const double pa = p.start_pa + i * p.step_pa;
        em.oss << "; band " << i << " PA=" << num(pa, 4) << "\n";
        em.oss << pa_command(p, pa);
        em.travel(L.x0, y);
        em.unretract();
        em.extrude(L.x0 + Lslow,         y, p.slow_speed);
        em.extrude(L.x0 + Lslow + Lfast, y, p.fast_speed);
        em.extrude(L.x0 + L.total_len,   y, p.slow_speed);
        em.retract();
    }
}

// Continuous serpentine of zig-zag bands, one PA per band, printed fast so the
// sharp peaks reveal PA. Side connectors double as the anchoring frame.
void build_pattern_body(Emitter& em, const PATestParams& p, int bands)
{
    const PatternLayout PL = compute_pattern_layout(p, bands);
    const double x_left  = PL.x_left;
    const double x_right = PL.x_right;
    const double y0      = PL.y0;
    const double spacing = PL.spacing;
    const double amp     = PL.amp;
    const double tooth_w = PL.tooth_w;
    const int    teeth   = PL.teeth;

    em.retract();   // balance the unretract below so there is no start blob
    em.travel(x_left, y0);
    em.unretract();
    for (int i = 0; i < bands; ++i) {
        const double y_base = y0 + i * spacing;
        const double pa     = p.start_pa + i * p.step_pa;
        const bool   l2r    = (i % 2 == 0);
        em.oss << "; band " << i << " PA=" << num(pa, 4) << "\n";
        em.oss << pa_command(p, pa);
        for (int t = 0; t < teeth; ++t) {
            const double peak = l2r ? x_left + (t + 0.5) * tooth_w : x_right - (t + 0.5) * tooth_w;
            const double vall = l2r ? x_left + (t + 1.0) * tooth_w : x_right - (t + 1.0) * tooth_w;
            em.extrude(peak, y_base + amp, p.fast_speed);
            em.extrude(vall, y_base,       p.fast_speed);
        }
        if (i < bands - 1) // side connector up to the next band (the frame)
            em.extrude(em.cur_x, y0 + (i + 1) * spacing, p.slow_speed);
    }
    em.retract();
}

void emit_builtin_start(std::ostringstream& oss, const PATestParams& p)
{
    oss << "; built-in start sequence (no mesh leveling)\n";
    oss << "M104 S" << p.nozzle_temp << "\n";
    oss << "M140 S" << p.bed_temp << "\n";
    oss << "G28\n";
    oss << "M190 S" << p.bed_temp << "\n";
    oss << "M109 S" << p.nozzle_temp << "\n";
}

void emit_builtin_end(std::ostringstream& oss, const PATestParams& p)
{
    oss << "; built-in end sequence\n";
    oss << "G1 " << axis_of(p) << "-" << num(retract_e_of(p), 5) << " F2400\n";
    oss << "G91\nG1 Z5 F600\nG90\n";
    oss << "M104 S0\nM140 S0\nM107\n";
    oss << "G28 X Y\nM84\n";
}

} // namespace

PATestFootprint pa_test_footprint(const PATestParams& p)
{
    const int bands = pa_test_band_count(p);
    if (p.kind == PATestKind::Line) {
        const LineLayout L = compute_line_layout(p, bands);
        return { L.x0, L.y0, L.x0 + L.total_len, L.y0 + (bands - 1) * L.spacing };
    }
    const PatternLayout PL = compute_pattern_layout(p, bands);
    return { PL.x_left, PL.y0, PL.x_right, PL.y0 + (bands - 1) * PL.spacing + PL.amp };
}

std::string generate_pa_test_gcode(const PATestParams& p)
{
    std::ostringstream oss;
    oss.imbue(std::locale::classic());

    const int    bands = pa_test_band_count(p);
    const double lw    = pa_test_line_width(p);
    const double epm   = pa_test_e_per_mm(p);

    // "generated by PrusaSlicer" lets GCodeProcessor recognize the producer and
    // recover the appended config block (extrusion axis, flow, speeds) on load.
    oss << "; generated by PrusaSlicer Filament Edition - PA "
        << (p.kind == PATestKind::Line ? "line" : "pattern") << " test\n";
    oss << "; sweep " << num(p.start_pa, 4) << " .. " << num(p.end_pa, 4)
        << " step " << num(p.step_pa, 4) << " (" << bands << " bands, front=start)\n";
    oss << "; line_width=" << num(lw, 3) << " layer_height=" << num(p.layer_height, 3)
        << " e_per_mm=" << num(epm, 5) << "\n";

    if (!p.start_gcode.empty()) {
        // Does the custom start G-code set temperatures itself? (line-start
        // match avoids matching M-codes inside comments).
        auto start_has = [&](std::initializer_list<const char*> codes) {
            std::istringstream in(p.start_gcode);
            std::string line;
            while (std::getline(in, line)) {
                const size_t i = line.find_first_not_of(" \t");
                if (i == std::string::npos) continue;
                for (const char* c : codes)
                    if (line.compare(i, std::string(c).size(), c) == 0) return true;
            }
            return false;
        };
        const bool sets_bed    = start_has({"M140", "M190"});
        const bool sets_nozzle = start_has({"M104", "M109"});

        // Mirror GCode.cpp's auto temperature emission around the custom start
        // G-code (printers relying on autoemit_temperature_commands don't put
        // M104/M109 in start_gcode). Without this the body can extrude cold.
        if (p.autoemit_temps && !sets_bed) {
            oss << "M140 S" << p.bed_temp << "\n";     // set bed
            oss << "M190 S" << p.bed_temp << "\n";     // wait bed (before start gcode)
        }
        if (p.autoemit_temps && !sets_nozzle)
            oss << "M104 S" << p.nozzle_temp << "\n";  // set nozzle, no wait

        oss << p.start_gcode;
        if (p.start_gcode.back() != '\n')
            oss << "\n";

        if (p.autoemit_temps && !sets_nozzle)
            oss << "M109 S" << p.nozzle_temp << "\n";  // wait nozzle (after start gcode)
    } else {
        emit_builtin_start(oss, p);
    }

    // Select the initial tool on multi-tool printers (GCode.cpp does this after
    // the start G-code). Single-tool printers leave T0 implicit.
    if (p.extruder_count > 1)
        oss << "T0\n";

    // Common prologue: mm units, absolute XYZ, relative E, drop to first-layer
    // height. G21 matches GCodeWriter::preamble() — without it a printer left in
    // inch mode (or a start macro that switched units) interprets the moves 25.4x.
    const std::string ax = axis_of(p);
    oss << "G21\nG90\nM83\nG92 " << ax << "0\n";
    // Printed Z includes the profile z_offset (matches GCode.cpp print_z + z_offset).
    oss << "G1 Z" << num(p.layer_height + p.z_offset, 3) << " F" << num(p.travel_speed * 60.0, 0) << "\n";

    Emitter em{oss, p, epm};
    if (p.kind == PATestKind::Line)
        build_line_body(em, p, bands);
    else
        build_pattern_body(em, p, bands);

    oss << "; reset pressure advance\n";
    oss << pa_command(p, 0.0);

    if (!p.end_gcode.empty()) {
        // The body ran in relative E (M83). The profile's end G-code expects
        // the profile's own mode — restore M82 (with a clean baseline) for
        // absolute-E printers so its retract/unload moves don't run relative.
        if (!p.relative_e)
            oss << "M82\nG92 " << ax << "0\n";
        oss << p.end_gcode;
        if (p.end_gcode.back() != '\n')
            oss << "\n";
    } else {
        emit_builtin_end(oss, p);   // built-in end is written for relative E
    }

    return oss.str();
}

} // namespace Slic3r
