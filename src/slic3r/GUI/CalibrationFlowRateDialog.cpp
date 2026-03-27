///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationFlowRateDialog.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "Tab.hpp"

#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Model.hpp"
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

CalibrationFlowRateDialog::CalibrationFlowRateDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("Flow Rate Calibration"),
               wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
    wxGetApp().UpdateDarkUI(this);

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    // Description
    auto* desc = new wxStaticText(this, wxID_ANY,
        _L("Generates flat pads with varying extrusion width to calibrate flow rate.\n"
           "Compare the top surface quality of each pad to find the optimal flow.\n"
           "Inspired by OrcaSlicer's YOLO Flow Calibration."));
    sizer->Add(desc, 0, wxALL, 15);

    auto* grid = new wxFlexGridSizer(5, 2, 10, 15);

    // Number of steps each side
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Steps each side of center:")),
              0, wxALIGN_CENTER_VERTICAL);
    m_num_steps = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 10, 5);
    grid->Add(m_num_steps, 0, wxEXPAND);

    // Step size
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Step size (%):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_step_pct = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 10, 1);
    grid->Add(m_step_pct, 0, wxEXPAND);

    // Pad dimensions
    grid->Add(new wxStaticText(this, wxID_ANY, _L("Pad width (mm):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_pad_width = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 5, 50, 10);
    grid->Add(m_pad_width, 0, wxEXPAND);

    grid->Add(new wxStaticText(this, wxID_ANY, _L("Pad depth (mm):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_pad_depth = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 10, 100, 30);
    grid->Add(m_pad_depth, 0, wxEXPAND);

    grid->Add(new wxStaticText(this, wxID_ANY, _L("Pad height (mm):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_pad_height = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0.4, 5.0, 2.0, 0.2);
    m_pad_height->SetDigits(1);
    grid->Add(m_pad_height, 0, wxEXPAND);

    sizer->Add(grid, 0, wxALL | wxEXPAND, 15);

    m_brim = new wxCheckBox(this, wxID_ANY, _L("Add 5 mm brim"));
    m_brim->SetValue(true);
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

bool CalibrationFlowRateDialog::generate_and_load()
{
    int    num_steps  = m_num_steps->GetValue();
    int    step_pct   = m_step_pct->GetValue();
    int    pad_w      = m_pad_width->GetValue();
    int    pad_d      = m_pad_depth->GetValue();
    double pad_h      = m_pad_height->GetValue();

    int total_pads = 2 * num_steps + 1;  // e.g. -5% to +5% = 11 pads

    // Get current nozzle diameter and extrusion width
    double nozzle_diameter = 0.4;
    double base_extrusion_width = 0.0;  // 0 = auto
    const PresetBundle* pb = wxGetApp().preset_bundle;
    if (pb) {
        const auto* nd_opt = pb->printers.get_selected_preset()
                                 .config.option<ConfigOptionFloats>("nozzle_diameter");
        if (nd_opt && !nd_opt->empty())
            nozzle_diameter = nd_opt->get_at(0);

        // Try to get the current perimeter extrusion width
        const auto* ew_opt = pb->prints.get_selected_preset()
                                 .config.option<ConfigOptionFloatOrPercent>("perimeter_extrusion_width");
        if (ew_opt && ew_opt->value > 0)
            base_extrusion_width = ew_opt->percent
                ? nozzle_diameter * ew_opt->value / 100.0
                : ew_opt->value;
    }

    // If auto (0), compute default extrusion width
    if (base_extrusion_width <= 0.0)
        base_extrusion_width = nozzle_diameter * 1.125;  // PrusaSlicer default auto width

    BOOST_LOG_TRIVIAL(info) << "Flow rate calibration: nozzle=" << nozzle_diameter
                            << " base_ew=" << base_extrusion_width
                            << " pads=" << total_pads
                            << " step=" << step_pct << "%";

    // Generate individual pad STLs and load them
    Plater* plater = wxGetApp().plater();
    if (!plater) return false;

    // Reset print config
    wxGetApp().preset_bundle->prints.discard_current_changes();
    {
        DynamicPrintConfig& config =
            wxGetApp().preset_bundle->prints.get_edited_preset().config;
        config.set_key_value("variable_layer_height", new ConfigOptionBool(false));
        if (m_brim && m_brim->GetValue())
            config.set_key_value("brim_width", new ConfigOptionFloat(5.0));
        // Solid top surface for flow comparison
        config.set_key_value("top_solid_layers", new ConfigOptionInt(3));
        config.set_key_value("bottom_solid_layers", new ConfigOptionInt(3));
        wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
    }

    // Gap between pads
    double gap = 2.0;
    double total_width = total_pads * pad_w + (total_pads - 1) * gap;

    // Generate and load each pad as a separate object
    boost::filesystem::path tmp_dir = boost::filesystem::temp_directory_path();

    std::vector<boost::filesystem::path> stl_paths;
    for (int i = 0; i < total_pads; ++i) {
        int flow_offset_pct = -num_steps * step_pct + i * step_pct;

        // Create a simple cube for each pad
        auto pad = its_make_cube(pad_w, pad_d, pad_h);

        // Position: spread pads along X
        double x_pos = i * (pad_w + gap) - total_width / 2.0;
        its_translate(pad, Vec3f(float(x_pos), float(-pad_d / 2.0), 0.f));

        // Format flow label for filename
        std::string label;
        if (flow_offset_pct == 0)
            label = "0";
        else if (flow_offset_pct > 0)
            label = "+" + std::to_string(flow_offset_pct);
        else
            label = std::to_string(flow_offset_pct);

        std::string filename = "flowrate_" + label + "pct.stl";
        boost::filesystem::path stl_path = tmp_dir / filename;
        std::string stl_str = stl_path.string();

        if (!its_write_stl_binary(stl_str.c_str(), filename.c_str(), pad)) {
            wxMessageBox(_L("Failed to write flow rate pad STL."),
                         _L("Error"), wxOK | wxICON_ERROR, this);
            return false;
        }
        stl_paths.push_back(stl_path);
    }

    // Load all pads
    std::vector<size_t> obj_indices = plater->load_files(stl_paths, true, false);

    // Apply per-object extrusion width overrides
    Model& model = wxGetApp().model();
    size_t num_objects = model.objects.size();

    for (int i = 0; i < total_pads && i < (int)num_objects; ++i) {
        int flow_offset_pct = -num_steps * step_pct + i * step_pct;
        double scale = 1.0 + flow_offset_pct / 100.0;
        double adjusted_ew = base_extrusion_width * scale;

        // Set per-object extrusion width override
        ModelObject* obj = model.objects[num_objects - total_pads + i];

        // Name the object with its flow offset
        std::string label;
        if (flow_offset_pct == 0)
            label = "0%";
        else if (flow_offset_pct > 0)
            label = "+" + std::to_string(flow_offset_pct) + "%";
        else
            label = std::to_string(flow_offset_pct) + "%";
        obj->name = "Flow " + label;

        // Override all extrusion widths for this object
        obj->config.set_key_value("perimeter_extrusion_width",
            new ConfigOptionFloatOrPercent(adjusted_ew, false));
        obj->config.set_key_value("external_perimeter_extrusion_width",
            new ConfigOptionFloatOrPercent(adjusted_ew, false));
        obj->config.set_key_value("infill_extrusion_width",
            new ConfigOptionFloatOrPercent(adjusted_ew, false));
        obj->config.set_key_value("solid_infill_extrusion_width",
            new ConfigOptionFloatOrPercent(adjusted_ew, false));
        obj->config.set_key_value("top_infill_extrusion_width",
            new ConfigOptionFloatOrPercent(adjusted_ew, false));

        BOOST_LOG_TRIVIAL(info) << "Flow pad " << label
                                << ": ew=" << adjusted_ew << "mm"
                                << " (scale=" << scale << ")";
    }

    // Clean up temp files
    for (const auto& p : stl_paths)
        boost::filesystem::remove(p);

    return true;
}

}} // namespace Slic3r::GUI
