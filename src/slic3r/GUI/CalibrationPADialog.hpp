///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationPADialog_hpp_
#define slic3r_CalibrationPADialog_hpp_

#include <wx/dialog.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/spinctrl.h>

namespace Slic3r { namespace GUI {

/// Dialog for pressure advance (PA) calibration. Three test styles:
///  - Tower: nested V-shaped chevrons, one PA value per layer group (sliced
///    STL + per-Z PA command); pick the height with the sharpest tips.
///  - Line / Pattern: flat single-layer tests where PA varies horizontally
///    per band. These are emitted as direct G-code (the slice pipeline can't
///    change PA within a layer) and loaded straight into the preview.
class CalibrationPADialog : public wxDialog
{
public:
    CalibrationPADialog(wxWindow* parent);

private:
    bool generate_and_load();
    bool generate_tower();       // chevron STL + per-Z PA (original path)
    bool generate_flat_test();   // direct-G-code line / pattern

    wxChoice*         m_mode{nullptr};       // 0 = Tower, 1 = Line, 2 = Pattern
    wxSpinCtrlDouble* m_start_pa{nullptr};
    wxSpinCtrlDouble* m_end_pa{nullptr};
    wxSpinCtrlDouble* m_pa_step{nullptr};
    wxSpinCtrlDouble* m_test_speed{nullptr}; // mm/s — tower speed / line+pattern fast speed
    wxCheckBox*       m_brim{nullptr};
};

}} // namespace Slic3r::GUI

#endif // slic3r_CalibrationPADialog_hpp_
