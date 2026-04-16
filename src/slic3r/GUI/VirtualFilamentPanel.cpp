#include "VirtualFilamentPanel.hpp"
#include "Plater.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"

#include "libslic3r/VirtualFilament.hpp"

#include <wx/dcmemory.h>
#include <wx/statline.h>
#include <wx/colour.h>
#include <wx/bitmap.h>

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

    // Title + enable checkbox
    auto *header_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_title = new wxStaticText(this, wxID_ANY, _L("Virtual Filaments"));
    m_title->SetFont(m_title->GetFont().Bold());
    header_sizer->Add(m_title, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

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

        // Component A color
        if (vf.component_a >= 1 && vf.component_a <= filament_colours.size()) {
            wxPanel *swatch_a = create_color_swatch(this, filament_colours[vf.component_a - 1], swatch_size / 2);
            row_sizer->Add(swatch_a, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 1);
        }

        // Component B color
        if (vf.component_b >= 1 && vf.component_b <= filament_colours.size()) {
            wxPanel *swatch_b = create_color_swatch(this, filament_colours[vf.component_b - 1], swatch_size / 2);
            row_sizer->Add(swatch_b, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        }

        // Label: "T1 + T2 (50%)"
        char label_buf[64];
        std::snprintf(label_buf, sizeof(label_buf), "T%u + T%u (%d%%)",
                      vf.component_a, vf.component_b, vf.mix_b_percent);
        auto *label = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(label_buf));
        if (!vf.enabled)
            label->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        row_sizer->Add(label, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

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
