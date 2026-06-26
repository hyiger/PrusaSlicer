///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationBedMeshDialog_hpp_
#define slic3r_CalibrationBedMeshDialog_hpp_

#include <wx/dialog.h>
#include <wx/checkbox.h>
#include <wx/spinctrl.h>

namespace Slic3r { namespace GUI {

// Configuration dialog shown before running a full bed probe. Collects
// nozzle and bed target temperatures plus optional flags (XL per-tool loop)
// and returns them via the accessors after ShowModal() == wxID_OK.
class CalibrationBedMeshDialog : public wxDialog
{
public:
    explicit CalibrationBedMeshDialog(wxWindow* parent);

    int  nozzle_temp_c()   const;
    int  bed_temp_c()      const;       // 0 → skip bed heating
    bool probe_all_tools() const;

private:
    wxSpinCtrl* m_nozzle_temp{nullptr};
    wxSpinCtrl* m_bed_temp{nullptr};
    wxCheckBox* m_heat_bed{nullptr};
    wxCheckBox* m_probe_all_tools{nullptr};
};

}} // namespace Slic3r::GUI

#endif // slic3r_CalibrationBedMeshDialog_hpp_
