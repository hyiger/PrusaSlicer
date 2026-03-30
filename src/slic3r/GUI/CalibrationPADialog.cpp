///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationPADialog.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "Tab.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/CalibrationModels.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/msgdlg.h>

#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>
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
    auto* grid  = new wxFlexGridSizer(3, 2, 10, 15);

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

    sizer->Add(grid, 0, wxALL | wxEXPAND, 15);

    m_brim = new wxCheckBox(this, wxID_ANY, _L("Add 5 mm brim"));
    m_brim->SetValue(false);
    sizer->Add(m_brim, 0, wxLEFT | wxRIGHT | wxBOTTOM, 15);

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

    // Reset print config to saved preset
    wxGetApp().preset_bundle->prints.discard_current_changes();

    // Configure for PA testing: ensure consistent layer height
    {
        DynamicPrintConfig& config =
            wxGetApp().preset_bundle->prints.get_edited_preset().config;
        config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
        config.set_key_value("variable_layer_height", new ConfigOptionBool(false));
        if (m_brim && m_brim->GetValue())
            config.set_key_value("brim_width", new ConfigOptionFloat(5.0));
        wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
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

    for (int i = 0; i < num_levels; ++i) {
        double z = i * PA_LAYERS_PER_LEVEL * layer_height + 0.1;
        double pa = start_pa + i * step;

        CustomGCode::Item item;
        item.print_z  = z;
        item.type     = CustomGCode::Custom;
        item.extruder = 1;
        item.color    = "";
        item.extra    = make_pa_gcode(pa);
        info.gcodes.push_back(item);
    }
    std::sort(info.gcodes.begin(), info.gcodes.end());

    // Clean up temp file
    boost::filesystem::remove(stl_path);

    return true;
}

}} // namespace Slic3r::GUI
