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
    : wxDialog(parent, wxID_ANY, _L("Flow Rate Calibration (YOLO)"),
               wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
    wxGetApp().UpdateDarkUI(this);

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* desc = new wxStaticText(this, wxID_ANY,
        _L("YOLO Flow Calibration — prints flat pads with Archimedean Chords\n"
           "spiral pattern at varying extrusion multipliers.\n"
           "Pick the pad with the smoothest top surface — no gaps between\n"
           "arcs and no material buildup at the inner spiral."));
    sizer->Add(desc, 0, wxALL, 15);

    auto* grid = new wxFlexGridSizer(4, 2, 10, 15);

    grid->Add(new wxStaticText(this, wxID_ANY, _L("Steps each side of center:")),
              0, wxALIGN_CENTER_VERTICAL);
    m_num_steps = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 10, 5);
    grid->Add(m_num_steps, 0, wxEXPAND);

    grid->Add(new wxStaticText(this, wxID_ANY, _L("Step size (%):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_step_pct = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 10, 1);
    grid->Add(m_step_pct, 0, wxEXPAND);

    grid->Add(new wxStaticText(this, wxID_ANY, _L("Pad width (mm):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_pad_width = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 20, 80, 30);
    grid->Add(m_pad_width, 0, wxEXPAND);

    grid->Add(new wxStaticText(this, wxID_ANY, _L("Pad depth (mm):")),
              0, wxALIGN_CENTER_VERTICAL);
    m_pad_depth = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 20, 80, 20);
    grid->Add(m_pad_depth, 0, wxEXPAND);

    sizer->Add(grid, 0, wxALL | wxEXPAND, 15);

    m_brim = new wxCheckBox(this, wxID_ANY, _L("Add 5 mm brim"));
    m_brim->SetValue(false);
    sizer->Add(m_brim, 0, wxLEFT | wxRIGHT | wxBOTTOM, 15);

    wxGetApp().UpdateDarkUI(m_num_steps);
    wxGetApp().UpdateDarkUI(m_step_pct);
    wxGetApp().UpdateDarkUI(m_pad_width);
    wxGetApp().UpdateDarkUI(m_pad_depth);
    wxGetApp().UpdateDarkUI(m_brim);

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
    int num_steps  = m_num_steps->GetValue();
    int step_pct   = m_step_pct->GetValue();
    int pad_w      = m_pad_width->GetValue();
    int pad_d      = m_pad_depth->GetValue();
    int total_pads = 2 * num_steps + 1;

    double nozzle_diameter = 0.4;
    const PresetBundle* pb = wxGetApp().preset_bundle;
    if (pb) {
        const auto* nd_opt = pb->printers.get_selected_preset()
                                 .config.option<ConfigOptionFloats>("nozzle_diameter");
        if (nd_opt && !nd_opt->empty())
            nozzle_diameter = nd_opt->get_at(0);
    }

    double layer_height = nozzle_diameter / 2.0;
    double pad_h = layer_height + 6.0 * layer_height;  // 7 layers

    BOOST_LOG_TRIVIAL(info) << "YOLO Flow calibration: nozzle=" << nozzle_diameter
                            << " layer_h=" << layer_height
                            << " pad_h=" << pad_h
                            << " pads=" << total_pads;

    Plater* plater = wxGetApp().plater();
    if (!plater) return false;

    // --- Print settings ---
    wxGetApp().preset_bundle->prints.discard_current_changes();
    {
        DynamicPrintConfig& config =
            wxGetApp().preset_bundle->prints.get_edited_preset().config;

        config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
        config.set_key_value("variable_layer_height", new ConfigOptionBool(false));

        if (m_brim && m_brim->GetValue())
            config.set_key_value("brim_width", new ConfigOptionFloat(5.0));
        else
            config.set_key_value("brim_width", new ConfigOptionFloat(0.0));

        config.set_key_value("perimeters", new ConfigOptionInt(1));
        // 6 solid monotonic layers + 1 spiral top layer
        config.set_key_value("top_solid_layers", new ConfigOptionInt(1));
        config.set_key_value("bottom_solid_layers", new ConfigOptionInt(6));
        config.set_key_value("fill_density", new ConfigOptionPercent(100));
        config.set_key_value("bottom_fill_pattern",
            new ConfigOptionEnum<InfillPattern>(ipMonotonic));
        config.set_key_value("top_fill_pattern",
            new ConfigOptionEnum<InfillPattern>(ipArchimedeanChords));
        config.set_key_value("ironing", new ConfigOptionBool(false));

        wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
        wxGetApp().plater()->on_config_change(wxGetApp().preset_bundle->full_config());
    }

    // --- Generate geometry ---
    boost::filesystem::path tmp_dir = boost::filesystem::temp_directory_path();
    std::vector<boost::filesystem::path> stl_paths;

    double corner_r = 1.5;
    double tab_w    = 17.0;
    double tab_d    = 8.0;
    double tab_h    = layer_height;  // single layer thick

    for (int i = 0; i < total_pads; ++i) {
        int flow_offset_pct = -num_steps * step_pct + i * step_pct;

        // Rectangular rounded pad
        auto pad = Slic3r::make_rounded_rect_pad(double(pad_w), double(pad_d), pad_h, corner_r);

        // Thin label tab at the back
        auto tab = Slic3r::make_rounded_rect_pad(tab_w, tab_d, tab_h, corner_r);
        double tab_x_offset = (double(pad_w) - tab_w) / 2.0;
        its_translate(tab, Vec3f(float(tab_x_offset), float(pad_d), 0.f));
        its_merge(pad, tab);

        // Raised text on the tab
        {
            char lbuf[16];
            std::string text_label;
            if (flow_offset_pct == 0)
                text_label = "0";
            else if (flow_offset_pct > 0) {
                snprintf(lbuf, sizeof(lbuf), ".%02d", flow_offset_pct);
                text_label = lbuf;
            } else {
                snprintf(lbuf, sizeof(lbuf), "-.%02d", -flow_offset_pct);
                text_label = lbuf;
            }

            double text_size = std::min(5.0, tab_d * 0.6);
            auto text_mesh = Slic3r::make_block_text(text_label, text_size, 0.4, false);
            if (!text_mesh.empty()) {
                for (auto& v : text_mesh.vertices)
                    std::swap(v.y(), v.z());
                for (auto& f : text_mesh.indices)
                    std::swap(f[0], f[1]);
                its_translate(text_mesh, Vec3f(
                    float(double(pad_w) / 2.0),
                    float(pad_d + tab_d / 2.0),
                    float(tab_h)));
                its_merge(pad, text_mesh);
            }
        }

        // Safe filename
        char label_buf[16];
        if (flow_offset_pct == 0)
            snprintf(label_buf, sizeof(label_buf), "0");
        else if (flow_offset_pct > 0)
            snprintf(label_buf, sizeof(label_buf), "p%02d", flow_offset_pct);
        else
            snprintf(label_buf, sizeof(label_buf), "m%02d", -flow_offset_pct);

        std::string filename = std::string("flow_") + label_buf + ".stl";
        boost::filesystem::path stl_path = tmp_dir / filename;

        if (!its_write_stl_binary(stl_path.string().c_str(), filename.c_str(), pad)) {
            wxMessageBox(_L("Failed to write flow rate pad STL."),
                         _L("Error"), wxOK | wxICON_ERROR, this);
            return false;
        }
        stl_paths.push_back(stl_path);
    }

    // --- Load and configure objects ---
    std::vector<size_t> loaded_object_idxs = plater->load_files(stl_paths, true, false);

    Model& model = wxGetApp().model();
    if (loaded_object_idxs.size() < (size_t)total_pads) {
        BOOST_LOG_TRIVIAL(error) << "YOLO Flow calibration: expected " << total_pads
                                 << " objects but only " << loaded_object_idxs.size() << " loaded";
        return false;
    }

    for (int i = 0; i < total_pads && i < (int)loaded_object_idxs.size(); ++i) {
        int flow_offset_pct = -num_steps * step_pct + i * step_pct;
        double multiplier = 1.0 + flow_offset_pct / 100.0;

        size_t obj_idx = loaded_object_idxs[i];
        if (obj_idx >= model.objects.size() || model.objects[obj_idx] == nullptr) {
            BOOST_LOG_TRIVIAL(error) << "YOLO Flow calibration: invalid object index " << obj_idx;
            return false;
        }
        ModelObject* obj = model.objects[obj_idx];

        // Object name = flow modifier label
        char name_buf[32];
        if (flow_offset_pct == 0)
            snprintf(name_buf, sizeof(name_buf), "0");
        else if (flow_offset_pct > 0)
            snprintf(name_buf, sizeof(name_buf), ".%02d", flow_offset_pct);
        else
            snprintf(name_buf, sizeof(name_buf), "-.%02d", -flow_offset_pct);
        obj->name = name_buf;

        // Per-object extrusion multiplier
        auto* em_opt = new ConfigOptionFloats();
        em_opt->values = { multiplier };
        obj->config.set_key_value("extrusion_multiplier", em_opt);

        // Enable special Archimedean Chords ordering (ported from OrcaSlicer)
        obj->config.set_key_value("calib_flowrate_topinfill_special_order",
            new ConfigOptionBool(true));
        BOOST_LOG_TRIVIAL(info) << "Flow pad " << obj->name
                                << ": extrusion_multiplier=" << multiplier;
    }

    // load_files() schedules a slice using the just-imported defaults, so the
    // per-object flow-rate overrides need an explicit invalidation pass.
    plater->changed_objects(loaded_object_idxs);
    plater->arrange(true);

    // Clean up temp files
    for (const auto& p : stl_paths)
        boost::filesystem::remove(p);

    return true;
}

}} // namespace Slic3r::GUI
