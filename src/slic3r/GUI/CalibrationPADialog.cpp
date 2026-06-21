///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationPADialog.hpp"
#include "CalibrationCommon.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "NotificationManager.hpp"
#include "Plater.hpp"
#include "Tab.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/CalibrationModels.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PlaceholderParser.hpp"
#include "libslic3r/GCode/CalibrationPATestGCode.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/msgdlg.h>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r { namespace GUI {

// Number of printed layers per PA level
static constexpr int PA_LAYERS_PER_LEVEL = 4;

CalibrationPADialog::CalibrationPADialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("Pressure Advance Calibration"),
               wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
    wxGetApp().UpdateDarkUI(this);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    auto* grid  = new wxFlexGridSizer(5, 2, 10, 15);

    // Test style: chevron tower (per-layer PA) vs. the flat line / pattern
    // tests (per-band PA, emitted as direct G-code).
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Test style:")),
              0, wxALIGN_CENTER_VERTICAL);
    m_mode = new wxChoice(this, wxID_ANY);
    m_mode->Append(_L("Chevron tower (per-layer PA)"));
    m_mode->Append(_L("PA line (flat, direct G-code)"));
    m_mode->Append(_L("PA pattern (flat, direct G-code)"));
    m_mode->SetSelection(0);
    grid->Add(m_mode, 0, wxEXPAND);

    // Start PA
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Start PA:")),
              0, wxALIGN_CENTER_VERTICAL);
    m_start_pa = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString,
                                      wxDefaultPosition, wxDefaultSize,
                                      wxSP_ARROW_KEYS, 0.0, 2.0, 0.0, 0.005);
    m_start_pa->SetDigits(3);
    grid->Add(m_start_pa, 0, wxEXPAND);

    // End PA
    grid->Add(new wxStaticText(this, wxID_ANY, _L("End PA:")),
              0, wxALIGN_CENTER_VERTICAL);
    m_end_pa = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString,
                                    wxDefaultPosition, wxDefaultSize,
                                    wxSP_ARROW_KEYS, 0.0, 2.0, 0.1, 0.005);
    m_end_pa->SetDigits(3);
    grid->Add(m_end_pa, 0, wxEXPAND);

    // PA Step
    grid->Add(new wxStaticText(this, wxID_ANY, _L("PA Step:")),
              0, wxALIGN_CENTER_VERTICAL);
    m_pa_step = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString,
                                     wxDefaultPosition, wxDefaultSize,
                                     wxSP_ARROW_KEYS, 0.001, 0.5, 0.005, 0.001);
    m_pa_step->SetDigits(3);
    grid->Add(m_pa_step, 0, wxEXPAND);

    // Test print speed (mm/s).
    // Pressure-advance differences only manifest visibly at print speeds
    // ≳ 80 mm/s — the corner pressure spike scales with extrusion rate.
    // PrusaSlicer's default profiles print perimeters around 40–50 mm/s,
    // and on top of that the filament cooling logic slows fast-printing
    // layers further to give them time to cool. The net effect is that
    // every PA value produces an indistinguishably blurry corner.
    // Forcing 100 mm/s for the test, plus disabling cooling slowdown,
    // surfaces the actual sweet spot.
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Test Speed (mm/s):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_test_speed = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString,
                                        wxDefaultPosition, wxDefaultSize,
                                        wxSP_ARROW_KEYS, 30.0, 300.0, 100.0, 5.0);
    m_test_speed->SetDigits(0);
    grid->Add(m_test_speed, 0, wxEXPAND);

    sizer->Add(grid, 0, wxALL | wxEXPAND, 15);

    m_brim = new wxCheckBox(this, wxID_ANY, _L("Add 5 mm brim"));
    m_brim->SetValue(false);
    sizer->Add(m_brim, 0, wxLEFT | wxRIGHT | wxBOTTOM, 15);

    // The brim only applies to the sliced chevron tower. The flat line/pattern
    // tests are direct G-code with no brim, so disable the control for them
    // rather than silently ignoring it.
    auto sync_brim_enabled = [this]() { m_brim->Enable(m_mode->GetSelection() == 0); };
    m_mode->Bind(wxEVT_CHOICE, [sync_brim_enabled](wxCommandEvent&) { sync_brim_enabled(); });
    sync_brim_enabled();

    wxGetApp().UpdateDarkUI(m_mode);
    wxGetApp().UpdateDarkUI(m_start_pa);
    wxGetApp().UpdateDarkUI(m_end_pa);
    wxGetApp().UpdateDarkUI(m_pa_step);
    wxGetApp().UpdateDarkUI(m_test_speed);
    wxGetApp().UpdateDarkUI(m_brim);

    // OK / Cancel
    auto* btns = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    wxGetApp().UpdateDarkUI(FindWindowById(wxID_OK, this));
    wxGetApp().UpdateDarkUI(FindWindowById(wxID_CANCEL, this));
    sizer->Add(btns, 0, wxEXPAND | wxALL, 10);

    SetSizer(sizer);
    sizer->SetSizeHints(this);
    Fit();
    Layout();
    CenterOnParent();

    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (generate_and_load())
            EndModal(wxID_OK);
    }, wxID_OK);
}

bool CalibrationPADialog::generate_and_load()
{
    const int mode = m_mode ? m_mode->GetSelection() : 0;
    return mode == 0 ? generate_tower() : generate_flat_test();
}

bool CalibrationPADialog::generate_tower()
{
    double start_pa = m_start_pa->GetValue();
    double end_pa   = m_end_pa->GetValue();
    double step     = m_pa_step->GetValue();

    if (start_pa >= end_pa) {
        wxMessageBox(_L("End PA must be greater than start PA."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }
    if (step <= 0.0) {
        wxMessageBox(_L("PA step must be positive."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    int num_levels = static_cast<int>(std::floor((end_pa - start_pa) / step)) + 1;
    if (num_levels < 2) {
        wxMessageBox(_L("PA range too small for the given step."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    // Read layer height from current print preset
    double layer_height = 0.2;
    const PresetBundle* pb = wxGetApp().preset_bundle;
    if (pb) {
        const auto* opt = pb->prints.get_selected_preset()
                              .config.option<ConfigOptionFloat>("layer_height");
        if (opt)
            layer_height = opt->getFloat();
    }

    // Total layers = levels × layers_per_level
    int total_layers = num_levels * PA_LAYERS_PER_LEVEL;

    // Format PA values for G-code comments
    auto fmt = [](double v) -> std::string {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.4f", v);
        return buf;
    };

    BOOST_LOG_TRIVIAL(info) << "Generating PA pattern: start=" << start_pa
                            << " end=" << end_pa << " step=" << step
                            << " levels=" << num_levels
                            << " total_layers=" << total_layers;

    // Generate single-chevron mesh
    auto its = Slic3r::make_pa_pattern(total_layers, layer_height);

    // Write to temp STL
    boost::filesystem::path stl_path =
        boost::filesystem::temp_directory_path() / "pa_pattern.stl";
    std::string stl_path_str = stl_path.string();
    if (!its_write_stl_binary(stl_path_str.c_str(), "pa_pattern", its)) {
        wxMessageBox(_L("Failed to write PA pattern STL."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    // Load onto bed
    Plater* plater = wxGetApp().plater();
    if (!plater) return false;

    std::vector<boost::filesystem::path> paths = { stl_path };
    plater->load_files(paths, true, false);

    // Reset BOTH print and filament configs to their saved state so that
    // the calibration overrides we're about to apply are deterministic
    // (don't stack on top of previous calibration runs or unrelated
    // user edits) AND so the modifications are visibly marked on the
    // presets as "changed since saved" — letting the user revert with
    // a single click on the preset's ⟲ button after calibration.
    wxGetApp().preset_bundle->prints.discard_current_changes();
    wxGetApp().preset_bundle->filaments.discard_current_changes();

    const double test_speed = m_test_speed ? m_test_speed->GetValue() : 100.0;

    // Configure for PA testing: ensure consistent layer height + uniform
    // high speed so PA differences are actually visible. The chevron is
    // mostly single-wall perimeter, so we override every perimeter speed
    // to the test value; infill speeds matter less but we set them too
    // for the inner walls that bridge the chevron tips.
    {
        DynamicPrintConfig& config =
            wxGetApp().preset_bundle->prints.get_edited_preset().config;
        config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
        config.set_key_value("variable_layer_height", new ConfigOptionBool(false));
        config.set_key_value("perimeter_speed",            new ConfigOptionFloat(test_speed));
        config.set_key_value("external_perimeter_speed",   new ConfigOptionFloatOrPercent(test_speed, false));
        config.set_key_value("small_perimeter_speed",      new ConfigOptionFloatOrPercent(test_speed, false));
        config.set_key_value("infill_speed",               new ConfigOptionFloat(test_speed));
        config.set_key_value("solid_infill_speed",         new ConfigOptionFloatOrPercent(test_speed, false));
        config.set_key_value("top_solid_infill_speed",     new ConfigOptionFloatOrPercent(test_speed, false));
        config.set_key_value("gap_fill_speed",             new ConfigOptionFloat(test_speed));
        if (m_brim && m_brim->GetValue())
            config.set_key_value("brim_width", new ConfigOptionFloat(5.0));
        else
            config.set_key_value("brim_width", new ConfigOptionFloat(0.0));
        wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
    }

    // Disable cooling-driven slowdown on the FILAMENT preset and pin the
    // slowdown floor at the requested test speed. Without this, the
    // chevron's small layers (each one prints in ~1–3 s) would trigger
    // PrusaSlicer's "slow down if layer print time < N seconds" logic and
    // every PA value would homogenize to the cooling-imposed min speed,
    // hiding the differences we're trying to surface.
    {
        DynamicPrintConfig& fil_config =
            wxGetApp().preset_bundle->filaments.get_edited_preset().config;
        fil_config.set_key_value("slowdown_below_layer_time",
                                 new ConfigOptionInts({0}));
        fil_config.set_key_value("min_print_speed",
                                 new ConfigOptionFloats({test_speed}));
        wxGetApp().get_tab(Preset::TYPE_FILAMENT)->reload_config();
    }

    // Determine PA command based on G-code flavor and printer model.
    //
    // Prusa printers all use gcfRepRapFirmware but differ in PA command:
    //   MINI uses M900 K<value> (Marlin-style linear advance)
    //   MK4/MK3.9/CORE ONE/XL use M572 S<value> (pressure advance)
    //
    // Non-Prusa firmware:
    //   Klipper uses SET_PRESSURE_ADVANCE ADVANCE=<value>
    //   Marlin uses M900 K<value>
    //   Generic RepRap uses M572 S<value>
    GCodeFlavor flavor = gcfRepRapFirmware;
    bool is_prusa_mini = false;
    if (pb) {
        const auto* flavor_opt = pb->printers.get_selected_preset()
                                     .config.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor");
        if (flavor_opt)
            flavor = flavor_opt->value;

        // Detect Prusa MINI by printer_model (uses legacy M900 K command)
        const auto* model_opt = pb->printers.get_selected_preset()
                                    .config.option<ConfigOptionString>("printer_model");
        if (model_opt && !model_opt->value.empty()) {
            std::string model = model_opt->value;
            // Convert to uppercase for case-insensitive match
            std::transform(model.begin(), model.end(), model.begin(), ::toupper);
            is_prusa_mini = (model.find("MINI") != std::string::npos);
        }
    }

    auto make_pa_gcode = [&](double pa_val) -> std::string {
        std::string val_str = fmt(pa_val);
        switch (flavor) {
        case gcfKlipper:
            return "SET_PRESSURE_ADVANCE ADVANCE=" + val_str + "\n";
        case gcfMarlinLegacy:
        case gcfMarlinFirmware:
            return "M900 K" + val_str + "\n";
        default:
            // RepRap firmware: MINI uses legacy M900 K, others use M572 S
            if (is_prusa_mini)
                return "M900 K" + val_str + "\n";
            return "M572 S" + val_str + "\n";
        }
    };

    // Insert per-layer PA commands.
    // Each level spans PA_LAYERS_PER_LEVEL layers.
    Model& model = wxGetApp().model();
    auto& info = model.custom_gcode_per_print_z();
    info.mode = CustomGCode::SingleExtruder;
    info.gcodes.clear();

    for (int i = 0; i < num_levels; ++i) {
        double z = i * PA_LAYERS_PER_LEVEL * layer_height + layer_height / 2.0;
        double pa = start_pa + i * step;

        CustomGCode::Item item;
        item.print_z  = z;
        item.type     = CustomGCode::Custom;
        item.extruder = 1;
        item.color    = "";
        item.extra    = make_pa_gcode(pa);
        info.gcodes.push_back(item);
    }

    // Reset PA to 0 after the last level so subsequent prints aren't affected
    {
        double z_top = (num_levels - 1) * PA_LAYERS_PER_LEVEL * layer_height
                     + PA_LAYERS_PER_LEVEL * layer_height - layer_height / 2.0;
        CustomGCode::Item reset;
        reset.print_z  = z_top;
        reset.type     = CustomGCode::Custom;
        reset.extruder = 1;
        reset.color    = "";
        reset.extra    = make_pa_gcode(0.0);
        info.gcodes.push_back(reset);
    }

    std::sort(info.gcodes.begin(), info.gcodes.end());

    // Clean up temp file
    boost::filesystem::remove(stl_path);

    // Surface the temporary preset overrides so the user knows to revert
    // before slicing other (non-calibration) models. Both presets carry
    // changes since their saved state (the Print preset tab and Filament
    // preset tab will both show the ⟲ revert affordance), but a banner
    // makes it obvious what was changed and why.
    if (auto* nm = wxGetApp().notification_manager()) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "PA calibration applied temporary overrides:\n"
            "  Print preset — perimeter / infill / gap-fill speeds → %.0f mm/s\n"
            "  Filament preset — cooling slowdown disabled, min_print_speed → %.0f mm/s\n"
            "Revert via the ⟲ buttons on the Print and Filament preset tabs "
            "before slicing other models.",
            test_speed, test_speed);
        nm->push_notification(
            NotificationType::CustomNotification,
            NotificationManager::NotificationLevel::WarningNotificationLevel,
            buf);
    }

    apply_calibration_filename_prefix("PressureAdvance");

    return true;
}

bool CalibrationPADialog::generate_flat_test()
{
    const int mode = m_mode ? m_mode->GetSelection() : 1;  // 1 = line, 2 = pattern

    const double start_pa = m_start_pa->GetValue();
    const double end_pa   = m_end_pa->GetValue();
    const double step     = m_pa_step->GetValue();
    if (start_pa >= end_pa) {
        wxMessageBox(_L("End PA must be greater than start PA."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }
    if (step <= 0.0) {
        wxMessageBox(_L("PA step must be positive."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }
    if (static_cast<int>(std::floor((end_pa - start_pa) / step + 1e-9)) + 1 < 2) {
        wxMessageBox(_L("PA range too small for the given step."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    const PresetBundle* pb = wxGetApp().preset_bundle;
    if (!pb) return false;
    const DynamicPrintConfig full = pb->full_config();

    auto first_float = [&](const char* k, double dflt) -> double {
        if (const auto* o = full.option<ConfigOptionFloats>(k); o && !o->empty())
            return o->get_at(0);
        return dflt;
    };
    auto first_int = [&](const char* k, int dflt) -> int {
        if (const auto* o = full.option<ConfigOptionInts>(k); o && !o->empty())
            return o->get_at(0);
        return dflt;
    };

    PATestParams p;
    p.kind     = (mode == 2) ? PATestKind::Pattern : PATestKind::Line;
    p.start_pa = start_pa;
    p.end_pa   = end_pa;
    p.step_pa  = step;

    p.nozzle_diameter      = first_float("nozzle_diameter", 0.4);
    p.filament_diameter    = first_float("filament_diameter", 1.75);
    p.extrusion_multiplier = first_float("extrusion_multiplier", 1.0);
    p.retract_length       = first_float("retract_length", 0.8);
    if (const auto* ea = full.option<ConfigOptionString>("extrusion_axis"); ea && !ea->value.empty())
        p.extrusion_axis = ea->value;
    if (const auto* tv = full.option<ConfigOptionFloat>("travel_speed"))
        p.travel_speed = tv->value;
    p.line_width = 0.0;  // generator derives 1.125 × nozzle

    // The flat tests are a single first-layer print, so use first_layer_height
    // (resolved over layer_height — Prusa low-layer profiles keep a thicker
    // first layer) for both the Z height and the flow calculation.
    if (const auto* lh = full.option<ConfigOptionFloat>("layer_height"))
        p.layer_height = lh->value;
    if (full.option("first_layer_height"))
        p.layer_height = full.get_abs_value("first_layer_height");
    if (const auto* zo = full.option<ConfigOptionFloat>("z_offset"))
        p.z_offset = zo->value;

    // Carry the printer's extrusion mode so the generator restores it before
    // the profile's (possibly absolute-E) end G-code.
    if (const auto* re = full.option<ConfigOptionBool>("use_relative_e_distances"))
        p.relative_e = re->value;

    const double fast = m_test_speed ? m_test_speed->GetValue() : 100.0;
    p.fast_speed   = fast;
    p.slow_speed   = std::max(20.0, fast * 0.3);
    p.anchor_speed = 20.0;

    p.nozzle_temp = first_int("first_layer_temperature", 215);
    p.bed_temp    = first_int("first_layer_bed_temperature", 60);
    if (const auto* ae = full.option<ConfigOptionBool>("autoemit_temperature_commands"))
        p.autoemit_temps = ae->value;

    // Bed extent from the bed_shape bounding box (test is centered within it).
    // Keep both min and max — delta / origin-offset beds don't start at (0,0).
    if (const auto* bs = full.option<ConfigOptionPoints>("bed_shape"); bs && !bs->values.empty()) {
        const BoundingBoxf bb(bs->values);
        p.bed_min_x  = bb.min.x();
        p.bed_min_y  = bb.min.y();
        p.bed_size_x = bb.max.x();
        p.bed_size_y = bb.max.y();
    }

    // Reject sweeps with too many bands to fit the bed — the flat test is direct
    // G-code, so an oversized pattern would otherwise send moves off the bed.
    if (!pa_test_fits_bed(p)) {
        wxMessageBox(
            wxString::Format(
                _L("This PA sweep needs %d bands, which do not fit on the bed. "
                   "Use a larger PA step (fewer bands) or a narrower PA range."),
                pa_test_band_count(p)),
            _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    // Validate the exact test footprint against the real bed polygon — the
    // bounding-box fit above is necessary but not sufficient for circular /
    // delta / clipped-corner beds where the corners lie outside the printable
    // area.
    const PATestFootprint fp = pa_test_footprint(p);
    if (const auto* bs = full.option<ConfigOptionPoints>("bed_shape"); bs && bs->values.size() >= 3) {
        Polygon bed;
        bed.points.reserve(bs->values.size());
        for (const Vec2d& v : bs->values)
            bed.points.emplace_back(Point::new_scale(v.x(), v.y()));
        const Vec2d corners[4] = {
            { fp.x_min, fp.y_min }, { fp.x_max, fp.y_min },
            { fp.x_max, fp.y_max }, { fp.x_min, fp.y_max } };
        for (const Vec2d& c : corners) {
            if (!bed.contains(Point::new_scale(c.x(), c.y()))) {
                wxMessageBox(
                    _L("This PA test would print outside the printable bed area. "
                       "Use a larger PA step (fewer bands) or a narrower PA range."),
                    _L("Error"), wxOK | wxICON_ERROR, this);
                return false;
            }
        }
    }

    // Firmware-specific PA command (mirrors the tower path).
    GCodeFlavor flavor = gcfRepRapFirmware;
    if (const auto* fo = full.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor"))
        flavor = fo->value;
    bool is_mini = false;
    if (const auto* mo = full.option<ConfigOptionString>("printer_model"); mo && !mo->value.empty()) {
        std::string m = mo->value;
        std::transform(m.begin(), m.end(), m.begin(), ::toupper);
        is_mini = m.find("MINI") != std::string::npos;
    }
    switch (flavor) {
    case gcfKlipper:        p.command = PATestCommand::Klipper; break;
    case gcfMarlinLegacy:
    case gcfMarlinFirmware: p.command = PATestCommand::M900; break;
    default:               p.command = is_mini ? PATestCommand::M900 : PATestCommand::M572; break;
    }

    // Substitute the printer's real start/end G-code (homing, mesh leveling,
    // priming, temperatures). Fall back to the generator's built-in minimal
    // sequence if a template fails to substitute outside the slice pipeline.
    bool have_start = false;
    {
        PlaceholderParser parser;
        DynamicPrintConfig cfg = full;   // apply_config takes an rvalue
        parser.apply_config(std::move(cfg));
        parser.update_timestamp();
        // Seed the geometry placeholders the slice pipeline would normally fill
        // before start-G-code processing (GCode.cpp). Prusa start G-code uses
        // these for the G80 mesh bed level / nozzle-cleanup area; without them
        // the template throws and we lose mesh leveling.
        parser.set("first_layer_print_min",  new ConfigOptionFloats({ fp.x_min, fp.y_min }));
        parser.set("first_layer_print_max",  new ConfigOptionFloats({ fp.x_max, fp.y_max }));
        parser.set("first_layer_print_size", new ConfigOptionFloats({ fp.x_max - fp.x_min, fp.y_max - fp.y_min }));
        parser.set("first_layer_print_convex_hull", new ConfigOptionPoints(
            { { fp.x_min, fp.y_min }, { fp.x_max, fp.y_min }, { fp.x_max, fp.y_max }, { fp.x_min, fp.y_max } }));
        parser.set("print_bed_min",  new ConfigOptionFloats({ p.bed_min_x, p.bed_min_y }));
        parser.set("print_bed_max",  new ConfigOptionFloats({ p.bed_size_x, p.bed_size_y }));
        parser.set("print_bed_size", new ConfigOptionFloats({ p.bed_size_x - p.bed_min_x, p.bed_size_y - p.bed_min_y }));
        // The remaining standard start-G-code placeholders the slice pipeline
        // sets (GCode.cpp), with single-layer calibration values, so profile
        // start macros referencing them substitute instead of throwing.
        int num_extruders = 1;
        if (const auto* nd = full.option<ConfigOptionFloats>("nozzle_diameter"); nd && !nd->empty())
            num_extruders = int(nd->size());
        std::vector<unsigned char> is_extruder_used(std::max<std::size_t>(255, std::size_t(num_extruders)), 0);
        is_extruder_used[0] = 1;
        parser.set("initial_tool", 0);
        parser.set("initial_extruder", 0);
        parser.set("current_extruder", 0);
        parser.set("current_object_idx", 0);
        parser.set("total_layer_count", 1);
        parser.set("total_toolchanges", 0);
        parser.set("num_extruders", num_extruders);
        parser.set("has_wipe_tower", false);
        parser.set("has_single_extruder_multi_material_priming", false);
        parser.set("is_extruder_used", new ConfigOptionBools(is_extruder_used));
        // End-G-code layer placeholders (GCode.cpp seeds these before end_gcode;
        // Prusa end macros use {layer_z}/{max_layer_z} for the Z-lift/park). The
        // flat test is a single layer at the first-layer height.
        parser.set("layer_num", 1);
        parser.set("layer_z", double(p.layer_height));
        parser.set("max_layer_z", double(p.layer_height));
        parser.set("filament_extruder_id", 0);
        // Guarded access — opt_string(key) would deref a null option if absent.
        auto opt_str = [&](const char* k) -> std::string {
            if (const auto* o = full.option<ConfigOptionString>(k)) return o->value;
            return {};
        };
        try {
            p.start_gcode = parser.process(opt_str("start_gcode"), 0);
            have_start = !p.start_gcode.empty();
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(warning) << "PA test: start_gcode substitution failed, using built-in: " << e.what();
        }
        try { p.end_gcode = parser.process(opt_str("end_gcode"), 0); }
        catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(warning) << "PA test: end_gcode substitution failed, using built-in: " << e.what();
        }
    }

    // If we couldn't use the printer's start sequence, the built-in fallback
    // homes and heats but does NOT mesh-level — make the user opt in.
    if (!have_start) {
        const int r = wxMessageBox(
            _L("Could not prepare the printer's start G-code for this test, so a minimal "
               "built-in sequence (home + heat, no mesh bed leveling) will be used. The first "
               "layer may not adhere well without leveling.\n\nGenerate the test anyway?"),
            _L("Pressure Advance test"), wxYES_NO | wxICON_WARNING, this);
        if (r != wxYES)
            return false;
    }

    std::string gcode = generate_pa_test_gcode(p);

    // Append the active config as the standard PrusaSlicer config block so the
    // G-code viewer's processor (which parses the bare temp file) recovers the
    // real settings — extrusion axis, flow, speeds, filament — instead of
    // assuming defaults (E axis), which would otherwise render the bands as
    // travel-only for non-E-axis printers. Use full_config_secure() (matches
    // the normal exporter) so print-host secrets aren't written into the file.
    {
        const DynamicPrintConfig secure_cfg = pb->full_config_secure();
        std::string cfg;
        for (const std::string& key : secure_cfg.keys())
            cfg += "; " + key + " = " + secure_cfg.opt_serialize(key) + "\n";
        gcode += "\n; prusaslicer_config = begin\n" + cfg + "; prusaslicer_config = end\n";
    }

    // Unique-per-parameters filename so re-generating actually reloads
    // (Plater::load_gcode skips an identical path).
    char name[96];
    std::snprintf(name, sizeof(name), "pa_%s_%d_%d_%d.gcode",
                  p.kind == PATestKind::Pattern ? "pattern" : "line",
                  int(std::lround(start_pa * 1000)), int(std::lround(end_pa * 1000)),
                  int(std::lround(step * 1000)));
    const boost::filesystem::path path = boost::filesystem::temp_directory_path() / name;
    {
        // boost::filesystem::ofstream + from_path() open/load via the native
        // (wide on Windows) path, so this works when the temp dir contains
        // non-ASCII characters (e.g. a localized Windows user name).
        boost::filesystem::ofstream ofs(path, std::ios::binary);
        if (!ofs) {
            wxMessageBox(_L("Failed to write the PA test G-code."),
                         _L("Error"), wxOK | wxICON_ERROR, this);
            return false;
        }
        ofs << gcode;
    }

    Plater* plater = wxGetApp().plater();
    if (!plater) return false;
    // Force a reload even when the path is unchanged — load_gcode early-returns
    // if m_last_loaded_gcode equals the filename, which would otherwise leave a
    // stale preview when the user regenerates with the same PA range.
    plater->reset_last_loaded_gcode();
    plater->load_gcode(from_path(path));

    if (auto* nm = wxGetApp().notification_manager()) {
        std::string msg =
            std::string("PA ") + (p.kind == PATestKind::Pattern ? "pattern" : "line") +
            " test generated as direct G-code and loaded in the preview.\n"
            "Front band = start PA, back = end PA. Review the first layer, then print.\n"
            "File: " + path.string();
        nm->push_notification(
            NotificationType::CustomNotification,
            NotificationManager::NotificationLevel::WarningNotificationLevel, msg);
    }

    BOOST_LOG_TRIVIAL(info) << "PA flat test (" << (p.kind == PATestKind::Pattern ? "pattern" : "line")
                            << ") written to " << path.string()
                            << " bands=" << pa_test_band_count(p);
    return true;
}

}} // namespace Slic3r::GUI
