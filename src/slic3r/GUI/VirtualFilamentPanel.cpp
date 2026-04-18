#include "VirtualFilamentPanel.hpp"
#include "Plater.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"

#include "libslic3r/VirtualFilament.hpp"

#include <wx/dcmemory.h>
#include <wx/statline.h>
#include <wx/colour.h>
#include <wx/bitmap.h>

#include <cmath>
#include <cstdio>

namespace Slic3r {
namespace GUI {

static wxColour hex_to_wx(const std::string &hex)
{
    if (hex.size() >= 7 && hex[0] == '#') {
        unsigned long val = 0;
        try { val = std::stoul(hex.substr(1, 6), nullptr, 16); } catch (...) {}
        return wxColour((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF);
    }
    return wxColour(128, 128, 128);
}

VirtualFilamentPanel::VirtualFilamentPanel(wxWindow *parent, Plater *plater)
    : wxPanel(parent, wxID_ANY)
    , m_plater(plater)
{
    auto *top_sizer = new wxBoxSizer(wxVERTICAL);

    // Title + "+ Add custom" button
    auto *header_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_title = new wxStaticText(this, wxID_ANY, _L("Virtual Filaments"));
    m_title->SetFont(m_title->GetFont().Bold());
    header_sizer->Add(m_title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    header_sizer->AddStretchSpacer(1);

    m_add_button = new wxButton(this, wxID_ANY, _L("+ Add custom…"),
                                wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    m_add_button->SetToolTip(_L("Create a custom virtual filament from a target color"));
    m_add_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        if (on_add_custom) on_add_custom();
    });
    header_sizer->Add(m_add_button, 0, wxALIGN_CENTER_VERTICAL);

    top_sizer->Add(header_sizer, 0, wxEXPAND | wxBOTTOM, 4);
    top_sizer->Add(new wxStaticLine(this, wxID_ANY), 0, wxEXPAND | wxBOTTOM, 4);

    // Scrollable area for rows
    m_rows_sizer = new wxBoxSizer(wxVERTICAL);
    top_sizer->Add(m_rows_sizer, 0, wxEXPAND);

    SetSizer(top_sizer);
}

wxPanel *VirtualFilamentPanel::create_color_swatch(wxWindow *parent, const std::string &hex_color, int size)
{
    auto *swatch = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(size, size));
    swatch->SetBackgroundColour(hex_to_wx(hex_color));
    swatch->SetMinSize(wxSize(size, size));
    return swatch;
}

void VirtualFilamentPanel::clear_rows()
{
    m_rows_sizer->Clear(true);
    m_row_sizers.clear();
}

void VirtualFilamentPanel::rebuild(const VirtualFilamentManager &mgr,
                                   const std::vector<std::string> &filament_colours,
                                   size_t num_physical)
{
    Freeze();
    clear_rows();

    const auto &filaments = mgr.filaments();
    size_t row_idx = 0;
    for (size_t i = 0; i < filaments.size(); ++i) {
        const auto &vf = filaments[i];
        if (vf.deleted) continue;

        auto *row_sizer = new wxBoxSizer(wxHORIZONTAL);

        // Color swatch (blended color)
        const int swatch_size = int(1.5 * wxGetApp().em_unit());
        wxPanel *swatch = create_color_swatch(this, vf.display_color, swatch_size);
        row_sizer->Add(swatch, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

        // Collect the physical IDs this virtual actually uses. Multi-component
        // custom entries (manual_pattern) can involve 3+ physicals, so derive
        // the set from the pattern rather than just component_a/component_b.
        std::vector<unsigned int> used_ids;
        std::vector<int>          used_counts;
        if (!vf.manual_pattern.empty()) {
            std::vector<int> count_by_id(10, 0); // tokens '1'..'9'
            for (char c : vf.manual_pattern) {
                if (c >= '1' && c <= '9') {
                    unsigned int tok = unsigned(c - '0');
                    unsigned int physical = tok;
                    if (tok == 1) physical = vf.component_a;
                    else if (tok == 2) physical = vf.component_b;
                    if (physical >= 1 && physical <= 9)
                        ++count_by_id[physical];
                }
            }
            for (size_t id = 1; id < count_by_id.size(); ++id)
                if (count_by_id[id] > 0) {
                    used_ids.push_back(unsigned(id));
                    used_counts.push_back(count_by_id[id]);
                }
        }
        if (used_ids.empty()) {
            used_ids  = {vf.component_a, vf.component_b};
            used_counts = {100 - vf.mix_b_percent, vf.mix_b_percent};
        }

        // Per-component mini swatches.
        for (size_t k = 0; k < used_ids.size(); ++k) {
            const unsigned int id = used_ids[k];
            if (id >= 1 && id <= filament_colours.size()) {
                wxPanel *sw = create_color_swatch(this, filament_colours[id - 1], swatch_size / 2);
                row_sizer->Add(sw, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                               (k + 1 == used_ids.size()) ? 4 : 1);
            }
        }

        // Label: "Name — T2 + T3 + T4 (33/58/8%)" if named, otherwise just
        //        "T2 + T3 + T4 (33/58/8%)".
        std::string ratio_text;
        for (size_t k = 0; k < used_ids.size(); ++k) {
            if (k > 0) ratio_text += " + ";
            ratio_text += "T" + std::to_string(used_ids[k]);
        }
        int pattern_total = 0;
        for (int c : used_counts) pattern_total += c;
        if (pattern_total > 0) {
            ratio_text += " (";
            for (size_t k = 0; k < used_counts.size(); ++k) {
                if (k > 0) ratio_text += "/";
                ratio_text += std::to_string(int(std::lround(
                    100.0 * double(used_counts[k]) / double(pattern_total))));
            }
            ratio_text += "%)";
        }
        std::string label_text;
        if (!vf.name.empty())
            label_text = vf.name + "  \xe2\x80\x94  " + ratio_text; // em-dash
        else
            label_text = ratio_text;
        auto *label = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(label_text));
        if (!vf.enabled)
            label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        row_sizer->Add(label, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

        // Edit button (pencil glyph).
        auto *edit_btn = new wxButton(this, wxID_ANY, wxString::FromUTF8("\xe2\x9c\x8f"), // ✏
                                      wxDefaultPosition, wxDefaultSize,
                                      wxBU_EXACTFIT);
        edit_btn->SetToolTip(_L("Edit this virtual filament"));
        const size_t edit_capture_idx = i;
        edit_btn->Bind(wxEVT_BUTTON, [this, edit_capture_idx](wxCommandEvent &) {
            if (on_edit_row) on_edit_row(edit_capture_idx);
        });
        row_sizer->Add(edit_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

        // Enable checkbox
        auto *cb = new wxCheckBox(this, wxID_ANY, "");
        cb->SetValue(vf.enabled);
        const size_t capture_idx = i;
        cb->Bind(wxEVT_CHECKBOX, [this, capture_idx](wxCommandEvent &evt) {
            if (on_enable_changed)
                on_enable_changed(capture_idx, evt.IsChecked());
        });
        row_sizer->Add(cb, 0, wxALIGN_CENTER_VERTICAL);

        m_rows_sizer->Add(row_sizer, 0, wxEXPAND | wxBOTTOM, 2);
        m_row_sizers.push_back(row_sizer);
        ++row_idx;
    }

    if (row_idx == 0) {
        auto *empty_label = new wxStaticText(this, wxID_ANY, _L("Load 2+ filaments to see virtual filaments"));
        empty_label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        m_rows_sizer->Add(empty_label, 0, wxALIGN_CENTER | wxTOP | wxBOTTOM, 4);
    }

    Layout();
    Thaw();
    GetParent()->Layout();
}

void VirtualFilamentPanel::show_panel(bool show)
{
    Show(show);
    if (GetContainingSizer())
        GetContainingSizer()->Layout();
}

} // namespace GUI
} // namespace Slic3r
