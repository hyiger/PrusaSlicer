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
#include "libslic3r/CalibrationModels.hpp"

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
        // 1 top layer makes over/under-extrusion most visible
        config.set_key_value("top_solid_layers", new ConfigOptionInt(1));
        config.set_key_value("bottom_solid_layers", new ConfigOptionInt(1));
        // Higher infill density provides better support for the single top layer
        config.set_key_value("fill_density", new ConfigOptionPercent(50));
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

        // T-shaped pad: wider label tab at back, narrower test surface in front
        double tab_d = 12.0;             // depth of the label tab
        double tab_w = pad_w + 14.0;     // wider than body for T-shape
        double body_w = double(pad_w);
        double body_d = double(pad_d);

        // Build pad body (test surface), centered in X relative to tab
        double body_x_offset = (tab_w - body_w) / 2.0;
        auto pad = its_make_cube(body_w, body_d, pad_h);
        its_translate(pad, Vec3f(float(body_x_offset), 0.f, 0.f));

        // Build tab (label area), full tab width, at back
        auto tab = its_make_cube(tab_w, tab_d, pad_h);
        its_translate(tab, Vec3f(0.f, float(body_d), 0.f));
        its_merge(pad, tab);

        // Add raised text label on top of the tab.
        // make_block_text returns text centered at origin in XZ plane, extruded in +Y.
        // We need it flat on XY at Z = pad_h, reading left-to-right in +X, top-to-bottom in +Y.
        {
            std::string text_label;
            if (flow_offset_pct == 0)
                text_label = "0%";
            else if (flow_offset_pct > 0)
                text_label = "+" + std::to_string(flow_offset_pct) + "%";
            else
                text_label = std::to_string(flow_offset_pct) + "%";

            // Size text to fit within the tab: max 4 chars like "-5%", pixel_size = tab_d / GLYPH_H
            double text_size = std::min(5.0, tab_d * 0.6);
            double text_raise = 0.5;
            auto text_mesh = Slic3r::make_block_text(text_label, text_size, text_raise, false);
            if (!text_mesh.empty()) {
                // Transform: XZ plane → XY plane, reading correctly from above.
                // Original: X = left/right, Z = up/down (text vertical), Y = depth (extrusion).
                // We want: X = left/right, Y = front/back (text vertical), Z = up (extrusion).
                // Mapping: X→X, Z→-Y (flip so top of text is toward -Y = front), Y→Z
                for (auto& v : text_mesh.vertices) {
                    float old_y = v.y();
                    float old_z = v.z();
                    v.y() = -old_z;  // text "up" becomes -Y (toward front of pad)
                    v.z() = old_y;   // extrusion depth becomes +Z (height)
                }
                // Also mirror X so text reads left-to-right when viewed from above
                for (auto& v : text_mesh.vertices)
                    v.x() = -v.x();
                // Fix face winding after mirror
                for (auto& f : text_mesh.indices)
                    std::swap(f[0], f[1]);

                // Position on tab center, on top of pad
                its_translate(text_mesh, Vec3f(
                    float(tab_w / 2.0),
                    float(body_d + tab_d / 2.0),
                    float(pad_h)));
                its_merge(pad, text_mesh);
            }
        }

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
    plater->load_files(stl_paths, true, false);

    // Apply per-object extrusion width overrides
    Model& model = wxGetApp().model();
    size_t num_objects = model.objects.size();

    for (int i = 0; i < total_pads && i < (int)num_objects; ++i) {
        int flow_offset_pct = -num_steps * step_pct + i * step_pct;
        double scale = 1.0 + flow_offset_pct / 100.0;
        double adjusted_ew = base_extrusion_width * scale;

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

    // Let PrusaSlicer's auto-arrange handle positioning
    plater->arrange(true);

    // Clean up temp files
    for (const auto& p : stl_paths)
        boost::filesystem::remove(p);

    return true;
}

}} // namespace Slic3r::GUI
