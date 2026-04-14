///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "CalibrationBedMeshDialog.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statbox.h>
#include <wx/button.h>

namespace Slic3r { namespace GUI {

CalibrationBedMeshDialog::CalibrationBedMeshDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, _L("Probe Bed Mesh"),
               wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
    wxGetApp().UpdateDarkUI(this);

    auto* top = new wxBoxSizer(wxVERTICAL);

    // Description — what's about to happen.
    top->Add(new wxStaticText(this, wxID_ANY,
        _L("This will home the printer, heat the nozzle (and optionally the bed), "
           "run G29 to probe the bed grid, and fetch the resulting mesh.\n\n"
           "Typical duration: 3–5 minutes (8–10 with bed heating).")),
        0, wxALL, 10);

    auto* temps_box   = new wxStaticBoxSizer(wxVERTICAL, this, _L("Temperatures"));
    auto* temps_grid  = new wxFlexGridSizer(3, 2, 8, 12);
    temps_grid->AddGrowableCol(1);

    // Nozzle temp.
    temps_grid->Add(new wxStaticText(this, wxID_ANY, _L("Nozzle (°C):")),
                    0, wxALIGN_CENTER_VERTICAL);
    m_nozzle_temp = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
                                   wxDefaultPosition, wxDefaultSize,
                                   wxSP_ARROW_KEYS, 100, 300, 170);
    temps_grid->Add(m_nozzle_temp, 0, wxEXPAND);

    // Heat-bed checkbox + temp. Heating the bed gives a more accurate mesh
    // because the PEI sheet flexes under thermal expansion (~0.05 mm), so
    // the cold-bed mesh systematically misses first-layer reality.
    m_heat_bed = new wxCheckBox(this, wxID_ANY, _L("Heat bed before probing"));
    m_heat_bed->SetValue(true);
    temps_grid->Add(m_heat_bed, 0, wxALIGN_CENTER_VERTICAL);

    m_bed_temp = new wxSpinCtrl(this, wxID_ANY, wxEmptyString,
                                wxDefaultPosition, wxDefaultSize,
                                wxSP_ARROW_KEYS, 0, 120, 60);
    temps_grid->Add(m_bed_temp, 0, wxEXPAND);

    // Row 3 — helper note.
    temps_grid->Add(0, 0);
    auto* hint = new wxStaticText(this, wxID_ANY,
        _L("Common: 60 for PLA, 85 for PETG, 100 for ABS."));
    wxFont hint_font = hint->GetFont();
    hint_font.SetPointSize(hint_font.GetPointSize() - 1);
    hint->SetFont(hint_font);
    temps_grid->Add(hint, 0, wxALIGN_CENTER_VERTICAL);

    temps_box->Add(temps_grid, 0, wxEXPAND | wxALL, 8);
    top->Add(temps_box, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

    // Disable the bed-temp spinner when the user unchecks heat-bed.
    m_heat_bed->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        m_bed_temp->Enable(m_heat_bed->GetValue());
    });

    // Per-tool option for XL. Harmless on other printers — they only have T0.
    auto* adv_box = new wxStaticBoxSizer(wxVERTICAL, this, _L("Advanced"));
    m_probe_all_tools = new wxCheckBox(this, wxID_ANY,
        _L("Probe all tools (XL: runs G29 per extruder, T0..Tn)"));
    m_probe_all_tools->SetValue(false);
    adv_box->Add(m_probe_all_tools, 0, wxALL, 8);
    top->Add(adv_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

    // OK / Cancel — relabel OK as "Start probing" to match the old dialog.
    auto* buttons = CreateButtonSizer(wxOK | wxCANCEL);
    if (auto* ok = FindWindow(wxID_OK))
        ok->SetLabel(_L("Start probing"));
    top->Add(buttons, 0, wxALL | wxALIGN_RIGHT, 10);

    SetSizerAndFit(top);
    CenterOnParent();
}

int CalibrationBedMeshDialog::nozzle_temp_c() const
{
    return m_nozzle_temp ? m_nozzle_temp->GetValue() : 170;
}

int CalibrationBedMeshDialog::bed_temp_c() const
{
    if (!m_heat_bed || !m_heat_bed->GetValue())
        return 0;
    return m_bed_temp ? m_bed_temp->GetValue() : 0;
}

bool CalibrationBedMeshDialog::probe_all_tools() const
{
    return m_probe_all_tools && m_probe_all_tools->GetValue();
}

}} // namespace Slic3r::GUI
