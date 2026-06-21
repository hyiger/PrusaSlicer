///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationPADialog.hpp"
#include "CalibrationCommon.hpp"
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
#include "libslic3r/BuildVolume.hpp"
#include "libslic3r/GCode/CalibrationPAPostProcessor.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/msgdlg.h>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
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

    // Test style
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Test style:")),
              0, wxALIGN_CENTER_VERTICAL);
    m_mode = new wxChoice(this, wxID_ANY);
    m_mode->Append(_L("Chevron tower (per-layer PA)"));
    m_mode->Append(_L("PA line (flat, per-band)"));
    m_mode->Append(_L("PA pattern (flat, per-band)"));
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
    return mode == 0 ? generate_tower() : generate_flat(mode);
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

bool CalibrationPADialog::generate_flat(int kind)   // 1 = line, 2 = pattern
{
    const double start_pa = m_start_pa->GetValue();
    const double end_pa   = m_end_pa->GetValue();
    const double step     = m_pa_step->GetValue();
    if (start_pa >= end_pa) {
        wxMessageBox(_L("End PA must be greater than start PA."), _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }
    if (step <= 0.0) {
        wxMessageBox(_L("PA step must be positive."), _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }
    const int num_bands = static_cast<int>(std::floor((end_pa - start_pa) / step + 1e-9)) + 1;
    if (num_bands < 2) {
        wxMessageBox(_L("PA range too small for the given step."), _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    const PresetBundle* pb = wxGetApp().preset_bundle;
    if (!pb) return false;
    double layer_height = 0.2;
    if (const auto* opt = pb->prints.get_selected_preset().config.option<ConfigOptionFloat>("layer_height"))
        layer_height = opt->getFloat();

    Plater* plater = wxGetApp().plater();
    if (!plater) return false;

    // Locale-invariant: band names are copied into the comma-delimited post-
    // processor URL, so a comma decimal separator would corrupt it.
    auto fmt4 = [](double v) {
        std::ostringstream s;
        s.imbue(std::locale::classic());
        s << std::fixed << std::setprecision(4) << v;
        return s.str();
    };

    // --- Geometry: one 1-layer chevron band per PA value ---
    const boost::filesystem::path tmp_dir = boost::filesystem::temp_directory_path();
    std::vector<boost::filesystem::path> stl_paths;
    stl_paths.reserve(num_bands);
    for (int i = 0; i < num_bands; ++i) {
        // Both styles are sharp 90° chevrons (a sharp corner is what reveals PA);
        // "line" uses longer arms (reads as a long line), "pattern" is compact.
        indexed_triangle_set its = (kind == 2)
            ? Slic3r::make_pa_pattern(1, layer_height, 90.0, 18.0, 1.6)
            : Slic3r::make_pa_pattern(1, layer_height, 90.0, 35.0, 1.2);
        if (its.vertices.empty() || its.indices.empty()) {
            wxMessageBox(_L("Failed to generate PA band geometry."), _L("Error"), wxOK | wxICON_ERROR, this);
            return false;
        }

        // Emboss the PA value as a printed label below the chevron so each band
        // is self-documenting — the user reads the value directly instead of
        // relying on bed position (which the arranger fallback may reorder).
        const double pa = start_pa + i * step;
        BoundingBoxf3 cb;
        for (const auto& v : its.vertices) cb.merge(v.cast<double>());
        indexed_triangle_set text = Slic3r::make_block_text(fmt4(pa), 5.0, layer_height, false);
        if (!text.empty()) {
            for (auto& v : text.vertices) std::swap(v.y(), v.z());   // lay the text flat
            for (auto& f : text.indices)  std::swap(f[0], f[1]);     // fix winding after the swap
            BoundingBoxf3 tb;
            for (const auto& v : text.vertices) tb.merge(v.cast<double>());
            const double tx = -0.5 * (tb.min.x() + tb.max.x());          // centre in X
            const double ty = (cb.min.y() - 2.0) - tb.max.y();           // 2 mm below the chevron
            its_translate(text, Vec3f(float(tx), float(ty), 0.0f));
            its_merge(its, text);
        }

        const std::string fname = "pa_band_" + std::to_string(i) + ".stl";
        boost::filesystem::path path = tmp_dir / fname;
        if (!its_write_stl_binary(path.string().c_str(), fname.c_str(), its)) {
            wxMessageBox(_L("Failed to write PA band STL."), _L("Error"), wxOK | wxICON_ERROR, this);
            return false;
        }
        stl_paths.push_back(path);
    }

    std::vector<size_t> loaded = plater->load_files(stl_paths, true, false);
    Model& model = wxGetApp().model();
    if (loaded.size() < (size_t)num_bands) {
        BOOST_LOG_TRIVIAL(error) << "PA flat calibration: expected " << num_bands
                                 << " bands, loaded " << loaded.size();
        return false;
    }

    // Name each band by its PA value — the key the post-processor matches.
    std::vector<std::pair<std::string, double>> object_pa;
    object_pa.reserve(num_bands);
    for (int i = 0; i < num_bands && i < (int)loaded.size(); ++i) {
        const double pa  = start_pa + i * step;
        ModelObject* obj = model.objects[loaded[i]];
        if (!obj) return false;
        obj->name = fmt4(pa);
        object_pa.emplace_back(obj->name, pa);
    }

    // --- Job-scoped calibration marker (first layer) ---
    // The whole test is a single layer, and assign_custom_gcodes() keeps only one
    // custom_gcode per layer — so the marker must be the SOLE entry, or a competing
    // first-layer entry (a stale marker, a user pause/color change) could evict it
    // and the post-processor would see no marker and inject no PA. Clear the list.
    {
        CustomGCode::Info& cg = model.custom_gcode_per_print_z();
        cg.mode = CustomGCode::SingleExtruder;
        cg.gcodes.clear();
        CustomGCode::Item marker;
        marker.print_z  = layer_height / 2.0;
        marker.type     = CustomGCode::Custom;
        marker.extruder = 1;
        marker.color    = "";
        marker.extra    = std::string("; ") + calibration_pa_marker() + "\n";
        cg.gcodes.push_back(marker);
    }

    // --- Speed overrides + OctoPrint labels (mirror the tower path) ---
    const double test_speed = m_test_speed ? m_test_speed->GetValue() : 100.0;
    wxGetApp().preset_bundle->prints.discard_current_changes();
    {
        DynamicPrintConfig& config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
        config.set_key_value("variable_layer_height", new ConfigOptionBool(false));
        // Sequential printing (complete_objects) takes the exporter's sequential
        // branch, which does NOT emit custom_gcode_per_print_z — our marker would
        // be absent and the post-processor would no-op. Force it off (as flow does).
        config.set_key_value("complete_objects", new ConfigOptionBool(false));
        config.set_key_value("perimeter_speed",          new ConfigOptionFloat(test_speed));
        config.set_key_value("external_perimeter_speed", new ConfigOptionFloatOrPercent(test_speed, false));
        config.set_key_value("small_perimeter_speed",    new ConfigOptionFloatOrPercent(test_speed, false));
        config.set_key_value("infill_speed",             new ConfigOptionFloat(test_speed));
        config.set_key_value("solid_infill_speed",       new ConfigOptionFloatOrPercent(test_speed, false));
        config.set_key_value("top_solid_infill_speed",   new ConfigOptionFloatOrPercent(test_speed, false));
        config.set_key_value("gap_fill_speed",           new ConfigOptionFloat(test_speed));
        // The bands are a single layer, so the WHOLE test is the first layer.
        // first_layer_speed (default 30 mm/s) is applied after the role speeds
        // and would otherwise clamp every band slow, hiding the PA differences.
        config.set_key_value("first_layer_speed",            new ConfigOptionFloatOrPercent(test_speed, false));
        config.set_key_value("first_layer_infill_speed",     new ConfigOptionFloatOrPercent(test_speed, false));
        config.set_key_value("first_layer_speed_over_raft",  new ConfigOptionFloatOrPercent(test_speed, false));
        config.set_key_value("brim_width", new ConfigOptionFloat(m_brim && m_brim->GetValue() ? 5.0 : 0.0));
        // OctoPrint labels emit "; printing object <name>" on every flavor so the
        // post-processor can tell the bands apart and key PA on the raw name.
        config.set_key_value("gcode_label_objects",
            new ConfigOptionEnum<LabelObjectsStyle>(LabelObjectsStyle::Octoprint));
        wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
    }
    wxGetApp().preset_bundle->filaments.discard_current_changes();
    {
        DynamicPrintConfig& fil = wxGetApp().preset_bundle->filaments.get_edited_preset().config;
        fil.set_key_value("slowdown_below_layer_time", new ConfigOptionInts({0}));
        fil.set_key_value("min_print_speed", new ConfigOptionFloats({test_speed}));
        wxGetApp().get_tab(Preset::TYPE_FILAMENT)->reload_config();
    }

    // --- Firmware PA command (mirror the tower detection) ---
    GCodeFlavor flavor = gcfRepRapFirmware;
    bool is_prusa_mini = false;
    if (const auto* fo = pb->printers.get_selected_preset().config.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor"))
        flavor = fo->value;
    if (const auto* mo = pb->printers.get_selected_preset().config.option<ConfigOptionString>("printer_model");
        mo && !mo->value.empty()) {
        std::string m = mo->value;
        std::transform(m.begin(), m.end(), m.begin(), ::toupper);
        is_prusa_mini = m.find("MINI") != std::string::npos;
    }
    PACalibrationCommand cmd;
    switch (flavor) {
    case gcfKlipper:        cmd = PACalibrationCommand::Klipper; break;
    case gcfMarlinLegacy:
    case gcfMarlinFirmware: cmd = PACalibrationCommand::M900; break;
    default:               cmd = is_prusa_mini ? PACalibrationCommand::M900 : PACalibrationCommand::M572; break;
    }

    // --- ASCII output so the post-processor can rewrite it ---
    // Override only binary_gcode on the printer preset (do NOT discard it — that
    // would wipe the user's unsaved start G-code / machine-limit / nozzle edits).
    {
        DynamicPrintConfig& printer = wxGetApp().preset_bundle->printers.get_edited_preset().config;
        printer.set_key_value("binary_gcode", new ConfigOptionBool(false));
        wxGetApp().get_tab(Preset::TYPE_PRINTER)->reload_config();
    }

    // --- Wire the in-process post-processor ---
    const std::string builtin_url = make_calibration_pa_url(cmd, 0.0, object_pa);
    {
        DynamicPrintConfig& print_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        std::vector<std::string> scripts;
        if (const auto* pp = print_config.option<ConfigOptionStrings>("post_process"))
            scripts = pp->values;
        scripts.erase(std::remove_if(scripts.begin(), scripts.end(),
                          [](const std::string& s) { return is_calibration_pa_url(s); }),
                      scripts.end());
        scripts.insert(scripts.begin(), builtin_url);
        print_config.set_key_value("post_process", new ConfigOptionStrings(scripts));
        wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
    }
    BOOST_LOG_TRIVIAL(info) << "PA flat calibration: " << num_bands << " bands, URL " << builtin_url;

    // --- Lay the bands out in a grid (front-to-back = start..end PA) ---
    bool placed = false;
    if (plater->build_volume().type() == BuildVolume::Type::Rectangle && !loaded.empty()) {
        const BoundingBoxf3 fp = model.objects[loaded[0]]->instance_bounding_box(0);
        const double cell_w = (fp.max.x() - fp.min.x()) + 4.0;
        const double cell_h = (fp.max.y() - fp.min.y()) + 4.0;
        const BoundingBoxf bed = plater->build_volume().bounding_volume2d();
        const Vec2d  bed_c    = bed.center();
        const double usable_w = bed.size().x() - 10.0;
        const double usable_h = bed.size().y() - 10.0;
        const int max_cols = std::max(1, int(std::floor(usable_w / cell_w)));
        int cols = std::min(num_bands, max_cols);
        int rows = (num_bands + cols - 1) / cols;
        while (rows * cell_h > usable_h && cols < max_cols) { ++cols; rows = (num_bands + cols - 1) / cols; }
        if (rows * cell_h <= usable_h && cols * cell_w <= usable_w) {
            const double left   = bed_c.x() - cols * cell_w / 2.0;
            const double bottom = bed_c.y() - rows * cell_h / 2.0;
            for (int i = 0; i < num_bands && i < (int)loaded.size(); ++i) {
                ModelObject* obj = model.objects[loaded[i]];
                if (obj->instances.empty()) continue;
                const int c = i % cols, r = i / cols;
                // Row 0 at the FRONT (min Y) so band 0 = start PA is at the front,
                // matching the docs/notification ("front = start PA, back = end").
                const Vec2d target(left + cell_w * (c + 0.5), bottom + cell_h * (r + 0.5));
                const BoundingBoxf3 bb = obj->instance_bounding_box(0);
                const Vec2d cur(0.5 * (bb.min.x() + bb.max.x()), 0.5 * (bb.min.y() + bb.max.y()));
                ModelInstance* inst = obj->instances.front();
                inst->set_offset(inst->get_offset() + Vec3d(target.x() - cur.x(), target.y() - cur.y(), 0.0));
                obj->invalidate_bounding_box();
            }
            placed = true;
        }
    }
    if (!placed)
        plater->arrange(true);
    plater->changed_objects(loaded);

    for (const auto& p : stl_paths)
        boost::filesystem::remove(p);

    if (auto* nm = wxGetApp().notification_manager()) {
        std::string msg =
            std::string("PA ") + (kind == 2 ? "pattern" : "line") +
            " test: one chevron band per PA value, each labeled with its PA "
            "(bands run front = start PA → back = end PA). Temporary speed overrides "
            "applied — revert via the ⟲ buttons on the Print/Filament tabs before "
            "slicing other models.";
        nm->push_notification(NotificationType::CustomNotification,
            NotificationManager::NotificationLevel::WarningNotificationLevel, msg);
    }
    apply_calibration_filename_prefix("PressureAdvance");
    return true;
}

}} // namespace Slic3r::GUI
