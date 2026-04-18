#include "CreateVirtualFilamentDialog.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"

#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/colordlg.h>
#include <wx/stdpaths.h>

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

static std::string wx_to_hex(const wxColour &c)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.Red(), c.Green(), c.Blue());
    return std::string(buf);
}

CreateVirtualFilamentDialog::CreateVirtualFilamentDialog(
    wxWindow                       *parent,
    const std::vector<std::string> &filament_colours,
    int                             default_denominator)
    : wxDialog(parent, wxID_ANY, _L("Create Custom Virtual Filament"),
               wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_filament_colours(filament_colours)
    , m_denominator(default_denominator)
{
    const int em = wxGetApp().em_unit();
    const int swatch_size = 4 * em;

    auto *top = new wxBoxSizer(wxVERTICAL);

    // ---- Input row ----------------------------------------------------
    auto *input_label = new wxStaticText(this, wxID_ANY,
        _L("Target color (hex like #FF6A00 or name like 'orange'):"));
    top->Add(input_label, 0, wxLEFT | wxRIGHT | wxTOP, em);

    auto *input_row = new wxBoxSizer(wxHORIZONTAL);
    m_input = new wxTextCtrl(this, wxID_ANY, "#FFA500");
    input_row->Add(m_input, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);

    m_pick_btn = new wxButton(this, wxID_ANY, _L("Pick…"));
    input_row->Add(m_pick_btn, 0, wxALIGN_CENTER_VERTICAL);

    top->Add(input_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, em);

    top->Add(new wxStaticLine(this, wxID_ANY), 0, wxEXPAND | wxALL, em / 2);

    // ---- Preview row: target | achieved ------------------------------
    auto *preview_row = new wxBoxSizer(wxHORIZONTAL);

    auto *target_col = new wxBoxSizer(wxVERTICAL);
    target_col->Add(new wxStaticText(this, wxID_ANY, _L("Target")),
                    0, wxALIGN_CENTER | wxBOTTOM, em / 4);
    m_target_swatch = new wxPanel(this, wxID_ANY, wxDefaultPosition,
                                  wxSize(swatch_size, swatch_size));
    m_target_swatch->SetMinSize(wxSize(swatch_size, swatch_size));
    target_col->Add(m_target_swatch, 0, wxALIGN_CENTER);
    preview_row->Add(target_col, 0, wxALL, em / 2);

    preview_row->AddStretchSpacer(1);

    auto *arrow = new wxStaticText(this, wxID_ANY, L"\u2192"); // →
    arrow->SetFont(arrow->GetFont().Scaled(1.5));
    preview_row->Add(arrow, 0, wxALIGN_CENTER_VERTICAL);

    preview_row->AddStretchSpacer(1);

    auto *achieved_col = new wxBoxSizer(wxVERTICAL);
    achieved_col->Add(new wxStaticText(this, wxID_ANY, _L("Achievable")),
                      0, wxALIGN_CENTER | wxBOTTOM, em / 4);
    m_achieved_swatch = new wxPanel(this, wxID_ANY, wxDefaultPosition,
                                    wxSize(swatch_size, swatch_size));
    m_achieved_swatch->SetMinSize(wxSize(swatch_size, swatch_size));
    achieved_col->Add(m_achieved_swatch, 0, wxALIGN_CENTER);
    preview_row->Add(achieved_col, 0, wxALL, em / 2);

    top->Add(preview_row, 0, wxEXPAND | wxLEFT | wxRIGHT, em);

    // ---- Status line (distance) --------------------------------------
    m_status_text = new wxStaticText(this, wxID_ANY, "");
    m_status_text->SetForegroundColour(
        wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    top->Add(m_status_text, 0, wxALIGN_CENTER | wxTOP | wxBOTTOM, em / 4);

    top->Add(new wxStaticLine(this, wxID_ANY), 0, wxEXPAND | wxALL, em / 2);

    // ---- Ratio bars --------------------------------------------------
    auto *ratio_label = new wxStaticText(this, wxID_ANY,
        _L("Filament mix (per cycle of ") +
        wxString::Format("%d", m_denominator) + _L(" layers):"));
    top->Add(ratio_label, 0, wxLEFT | wxRIGHT | wxTOP, em);

    m_ratio_bar_area  = new wxPanel(this, wxID_ANY);
    m_ratio_bar_sizer = new wxBoxSizer(wxVERTICAL);
    m_ratio_bar_area->SetSizer(m_ratio_bar_sizer);
    top->Add(m_ratio_bar_area, 0, wxEXPAND | wxLEFT | wxRIGHT, em);

    m_ratio_text = new wxStaticText(this, wxID_ANY, "");
    top->Add(m_ratio_text, 0, wxALIGN_CENTER | wxTOP | wxBOTTOM, em / 4);

    top->Add(new wxStaticLine(this, wxID_ANY), 0, wxEXPAND | wxALL, em / 2);

    // ---- Buttons -----------------------------------------------------
    auto *buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer(1);
    auto *cancel = new wxButton(this, wxID_CANCEL);
    buttons->Add(cancel, 0, wxRIGHT, em / 2);
    m_ok_btn = new wxButton(this, wxID_OK, _L("Create"));
    m_ok_btn->SetDefault();
    buttons->Add(m_ok_btn, 0);
    top->Add(buttons, 0, wxEXPAND | wxALL, em);

    SetSizerAndFit(top);

    // Events
    m_input->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { on_input_changed(); });
    m_pick_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { on_pick_color(); });

    // Initial compute with the default "#FFA500".
    recompute();
}

void CreateVirtualFilamentDialog::on_input_changed()
{
    recompute();
}

void CreateVirtualFilamentDialog::on_pick_color()
{
    wxColourData data;
    if (!m_target_hex.empty())
        data.SetColour(hex_to_wx(m_target_hex));
    data.SetChooseFull(true);

    wxColourDialog dlg(this, &data);
    if (dlg.ShowModal() == wxID_OK) {
        const wxColour picked = dlg.GetColourData().GetColour();
        m_input->ChangeValue(wx_to_hex(picked));
        recompute();
    }
}

void CreateVirtualFilamentDialog::recompute()
{
    m_target_hex = VirtualFilamentManager::parse_color_input(
        std::string(m_input->GetValue().ToUTF8()));

    if (m_target_hex.empty() || m_filament_colours.size() < 2) {
        m_solution = {};
        m_solution.target_color = m_target_hex;
        if (m_ok_btn) m_ok_btn->Enable(false);
    } else {
        m_solution = VirtualFilamentManager::solve_target_color(
            m_target_hex, m_filament_colours, m_denominator);
        if (m_ok_btn) m_ok_btn->Enable(true);
    }

    refresh_preview();
}

void CreateVirtualFilamentDialog::refresh_preview()
{
    // Target swatch
    if (m_target_swatch) {
        if (!m_target_hex.empty())
            m_target_swatch->SetBackgroundColour(hex_to_wx(m_target_hex));
        else
            m_target_swatch->SetBackgroundColour(
                wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
        m_target_swatch->Refresh();
    }

    // Achieved swatch
    if (m_achieved_swatch) {
        if (!m_solution.achieved_color.empty())
            m_achieved_swatch->SetBackgroundColour(hex_to_wx(m_solution.achieved_color));
        else
            m_achieved_swatch->SetBackgroundColour(
                wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
        m_achieved_swatch->Refresh();
    }

    // Status line
    if (m_status_text) {
        if (m_target_hex.empty()) {
            m_status_text->SetLabel(_L("Enter a valid color to preview."));
        } else if (m_filament_colours.size() < 2) {
            m_status_text->SetLabel(_L("Need at least 2 loaded filaments."));
        } else {
            wxString s = wxString::Format(_L("Target %s  ·  Achievable %s  ·  ΔRGB ≈ %.1f"),
                                          wxString::FromUTF8(m_target_hex),
                                          wxString::FromUTF8(m_solution.achieved_color),
                                          m_solution.rgb_distance);
            m_status_text->SetLabel(s);
        }
    }

    // Ratio bars
    if (m_ratio_bar_sizer) {
        m_ratio_bar_area->Freeze();
        m_ratio_bar_sizer->Clear(true);

        const int em = wxGetApp().em_unit();
        std::string summary;
        for (size_t i = 0; i < m_solution.ratios.size(); ++i) {
            if (m_solution.ratios[i] <= 0) continue;
            auto *row = new wxBoxSizer(wxHORIZONTAL);

            // Small physical-color swatch
            auto *sw = new wxPanel(m_ratio_bar_area, wxID_ANY, wxDefaultPosition,
                                   wxSize(em, em));
            sw->SetBackgroundColour(hex_to_wx(m_filament_colours[i]));
            sw->SetMinSize(wxSize(em, em));
            row->Add(sw, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 4);

            // Label "T1: 3/12"
            auto *lbl = new wxStaticText(m_ratio_bar_area, wxID_ANY,
                wxString::Format("T%zu: %d / %d", i + 1, m_solution.ratios[i], m_denominator));
            row->Add(lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, em / 2);

            // Proportional bar
            const int bar_w = (m_solution.ratios[i] * 15 * em) / std::max(1, m_denominator);
            auto *bar = new wxPanel(m_ratio_bar_area, wxID_ANY, wxDefaultPosition,
                                    wxSize(bar_w, em / 2));
            bar->SetBackgroundColour(hex_to_wx(m_filament_colours[i]));
            bar->SetMinSize(wxSize(bar_w, em / 2));
            row->Add(bar, 0, wxALIGN_CENTER_VERTICAL);

            m_ratio_bar_sizer->Add(row, 0, wxEXPAND | wxBOTTOM, em / 4);

            if (!summary.empty()) summary += "  ";
            summary += "T" + std::to_string(i + 1) + ":" +
                       std::to_string(m_solution.ratios[i]);
        }
        m_ratio_text->SetLabel(wxString::FromUTF8(summary));

        m_ratio_bar_area->Layout();
        m_ratio_bar_area->Thaw();
    }

    Layout();
    Fit();
}

} // namespace GUI
} // namespace Slic3r
