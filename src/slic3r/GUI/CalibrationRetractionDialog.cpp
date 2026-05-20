///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationRetractionDialog.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "Tab.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/CalibrationModels.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/GCode/CalibrationRetractionPostProcessor.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/msgdlg.h>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r { namespace GUI {

// Number of layers printed at each retraction value
static constexpr int LAYERS_PER_LEVEL = 5;

CalibrationRetractionDialog::CalibrationRetractionDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("Retraction Calibration"),
               wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
    wxGetApp().UpdateDarkUI(this);

    // Read current retraction length from printer preset as default
    double default_retract = 0.8;
    const PresetBundle* pb = wxGetApp().preset_bundle;
    if (pb) {
        const auto* opt = pb->printers.get_selected_preset()
                              .config.option<ConfigOptionFloats>("retract_length");
        if (opt && !opt->empty())
            default_retract = opt->get_at(0);
    }

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    auto* grid  = new wxFlexGridSizer(6, 2, 10, 15);

    // Start retraction
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Start retraction (mm):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_start_retract = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS,
        0.0, 10.0, std::max(0.0, default_retract - 1.0), 0.1);
    m_start_retract->SetDigits(1);
    grid->Add(m_start_retract, 0, wxEXPAND);

    // End retraction
    grid->Add(new wxStaticText(this, wxID_ANY, _L("End retraction (mm):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_end_retract = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS,
        0.0, 10.0, default_retract + 1.0, 0.1);
    m_end_retract->SetDigits(1);
    grid->Add(m_end_retract, 0, wxEXPAND);

    // Retraction step
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Retraction step (mm):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_retract_step = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS,
        0.01, 2.0, 0.1, 0.05);
    m_retract_step->SetDigits(2);
    grid->Add(m_retract_step, 0, wxEXPAND);

    // Tower height
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Tower height (mm):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_tower_height = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 10, 200, 100);
    grid->Add(m_tower_height, 0, wxEXPAND);

    // Tower diameter
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Tower diameter (mm):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_tower_diameter = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 3, 30, 10);
    grid->Add(m_tower_diameter, 0, wxEXPAND);

    // Tower spacing
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Tower spacing (mm):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_tower_spacing = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 10, 150, 34);
    grid->Add(m_tower_spacing, 0, wxEXPAND);

    sizer->Add(grid, 0, wxALL | wxEXPAND, 15);

    m_brim = new wxCheckBox(this, wxID_ANY, _L("Add 5 mm brim"));
    m_brim->SetValue(false);
    sizer->Add(m_brim, 0, wxLEFT | wxRIGHT | wxBOTTOM, 15);

    wxGetApp().UpdateDarkUI(m_start_retract);
    wxGetApp().UpdateDarkUI(m_end_retract);
    wxGetApp().UpdateDarkUI(m_retract_step);
    wxGetApp().UpdateDarkUI(m_tower_height);
    wxGetApp().UpdateDarkUI(m_tower_diameter);
    wxGetApp().UpdateDarkUI(m_tower_spacing);
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

bool CalibrationRetractionDialog::generate_and_load()
{
    double start   = m_start_retract->GetValue();
    double end     = m_end_retract->GetValue();
    double step    = m_retract_step->GetValue();
    int    height  = m_tower_height->GetValue();
    int    diam    = m_tower_diameter->GetValue();
    int    spacing = m_tower_spacing->GetValue();

    if (start >= end) {
        wxMessageBox(_L("End retraction must be greater than start retraction."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }
    if (step <= 0.0) {
        wxMessageBox(_L("Retraction step must be positive."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    int num_levels = static_cast<int>(std::round((end - start) / step)) + 1;
    if (num_levels < 2) {
        wxMessageBox(_L("Retraction range too small for the given step."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    // Get layer height from current print config
    double layer_height = 0.2;
    const PresetBundle* pb = wxGetApp().preset_bundle;
    if (pb) {
        const auto* opt = pb->prints.get_selected_preset()
                              .config.option<ConfigOptionFloat>("layer_height");
        if (opt)
            layer_height = opt->getFloat();
    }

    // Compute actual tower height to fit all levels evenly
    double level_height = LAYERS_PER_LEVEL * layer_height;
    double actual_height = num_levels * level_height;
    if (actual_height > height)
        actual_height = height;

    BOOST_LOG_TRIVIAL(info) << "Generating retraction towers: start=" << start
                            << " end=" << end << " step=" << step
                            << " levels=" << num_levels
                            << " height=" << actual_height;

    auto its = Slic3r::make_retraction_towers(actual_height, double(diam), double(spacing));

    BOOST_LOG_TRIVIAL(info) << "Retraction towers: verts=" << its.vertices.size()
                            << " faces=" << its.indices.size();

    if (its.vertices.empty() || its.indices.empty()) {
        wxMessageBox(_L("Failed to generate retraction tower geometry."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    // Write to temp STL
    boost::filesystem::path stl_path =
        boost::filesystem::temp_directory_path() / "retraction_towers.stl";
    std::string stl_path_str = stl_path.string();
    if (!its_write_stl_binary(stl_path_str.c_str(), "retraction_towers", its)) {
        wxMessageBox(_L("Failed to write retraction tower STL."),
                     _L("Error"), wxOK | wxICON_ERROR, this);
        return false;
    }

    // Load onto bed
    Plater* plater = wxGetApp().plater();
    if (!plater) return false;

    std::vector<boost::filesystem::path> paths = { stl_path };
    plater->load_files(paths, true, false);

    // Reset print config and apply overrides
    wxGetApp().preset_bundle->prints.discard_current_changes();
    {
        DynamicPrintConfig& config =
            wxGetApp().preset_bundle->prints.get_edited_preset().config;
        config.set_key_value("variable_layer_height", new ConfigOptionBool(false));
        // 0% infill keeps the cylinders hollow for stringing visibility.
        // top_solid_layers is left at the preset default so the base's top
        // surface (the annulus around each cylinder where the cross-section
        // shrinks at z=1mm) is solid — otherwise the cylinder first layer
        // prints into mid air and snaps off the base.
        config.set_key_value("fill_density", new ConfigOptionPercent(0));
        // Seam = nearest, which places the seam on the inward face of each
        // tower. This is REQUIRED, not cosmetic: the seam is the perimeter
        // start/end point, and the inter-tower travel runs seam-to-seam.
        // Inward seams route that travel straight across the observation gap,
        // so stringing forms where it can be seen. spRear would route the
        // travel along the back (+Y) instead — the strings would still form,
        // just out of view, making the gap look deceptively clean.
        // The seam also accumulates a small deretract blob each layer; that
        // ridge is itself a valid retraction signal (thick at low-retract
        // bands, clean at high-retract bands), not contamination.
        config.set_key_value("seam_position",
            new ConfigOptionEnum<SeamPosition>(spNearest));
        if (m_brim && m_brim->GetValue())
            config.set_key_value("brim_width", new ConfigOptionFloat(5.0));
        else
            config.set_key_value("brim_width", new ConfigOptionFloat(0.0));
        wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
    }

    // Read the printer's baseline retract_length so the base layers print
    // with the user's normal retraction setting, only varying inside the
    // tower section. Use the maximum test value as a slicer-side sentinel:
    // PrusaSlicer will emit explicit `G1 E-<end>` retracts at every travel,
    // and our post-process script rewrites each one based on Z.
    double base_retract = 0.7;
    wxGetApp().preset_bundle->printers.discard_current_changes();
    {
        DynamicPrintConfig& printer_config =
            wxGetApp().preset_bundle->printers.get_edited_preset().config;
        const auto* opt_rl = printer_config.option<ConfigOptionFloats>("retract_length");
        if (opt_rl && !opt_rl->empty())
            base_retract = opt_rl->get_at(0);

        // Buddy firmware (Core One / MK4 / MK4S / MK3.5 / MINI / iX / XL)
        // does NOT implement M207/G10/G11. Force slicer-side retraction so
        // PrusaSlicer emits real `G1 E-x` moves that the firmware actually
        // honors.
        printer_config.set_key_value("use_firmware_retraction", new ConfigOptionBool(false));

        // Force ASCII G-code output. PrusaSlicer writes binary G-code directly
        // during export and only THEN runs post-process scripts, so our text
        // rewriter sees a sealed .bgcode and has nothing to rewrite. Buddy
        // accepts .gcode just fine.
        printer_config.set_key_value("binary_gcode", new ConfigOptionBool(false));

        // Force relative E distances. The post-processor classifies a
        // retract-only `G1 E…` move by the sign of E (negative = retract,
        // positive = recovery). That only holds in relative-E mode; with
        // absolute E a retract is just a smaller absolute coordinate and is
        // usually still positive, so it would be misread as a recovery and
        // rewritten to a tiny absolute position. Pinning relative E keeps the
        // sign-based classification valid.
        printer_config.set_key_value("use_relative_e_distances", new ConfigOptionBool(true));

        // Force linear (non-volumetric) E. With volumetric extrusion the E
        // axis is in mm^3; the post-processor writes retraction lengths in
        // linear mm, so volumetric mode would scale every test band by the
        // filament cross-section and invalidate the calibration.
        printer_config.set_key_value("use_volumetric_e", new ConfigOptionBool(false));

        // Set retract_length to the maximum test value across every extruder
        // entry. Every travel will retract by `end` until the post-process
        // pass rewrites it.
        size_t num_extruders = opt_rl ? std::max<size_t>(opt_rl->size(), 1) : 1;
        std::vector<double> rl_values(num_extruders, end);
        printer_config.set_key_value("retract_length", new ConfigOptionFloats(rl_values));

        // Force retract == recovery so the rewrite is symmetric.
        std::vector<double> zero_values(num_extruders, 0.0);
        printer_config.set_key_value("retract_restart_extra", new ConfigOptionFloats(zero_values));

        wxGetApp().get_tab(Preset::TYPE_PRINTER)->reload_config();
    }

    // --- Wire up the in-process post-processor ---
    //
    // PrusaSlicer's `post_process` config accepts a list of script paths. We
    // use a `::builtin::` URL that PostProcessor.cpp intercepts and dispatches
    // to `Slic3r::run_calibration_retraction_post_processor()` — no external
    // interpreter, no temp script file, no platform dependencies.
    std::vector<std::pair<double, double>> levels;
    levels.reserve(num_levels);
    for (int i = 0; i < num_levels; ++i) {
        double z_top   = 1.0 + double(i + 1) * level_height;
        double retract = start + i * step;
        levels.emplace_back(z_top, retract);
    }
    std::string builtin_url = make_calibration_retraction_url(base_retract, levels);

    {
        DynamicPrintConfig& print_config =
            wxGetApp().preset_bundle->prints.get_edited_preset().config;
        print_config.set_key_value("post_process",
            new ConfigOptionStrings(std::vector<std::string>{ builtin_url }));
        wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
    }

    BOOST_LOG_TRIVIAL(info) << "Retraction calibration: post-process URL " << builtin_url
                            << ", base_retract=" << base_retract
                            << ", levels=" << num_levels;

    // Clean up temp STL
    boost::filesystem::remove(stl_path);

    return true;
}

}} // namespace Slic3r::GUI
