///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationPADialog_hpp_
#define slic3r_CalibrationPADialog_hpp_

#include <wx/dialog.h>
#include <wx/checkbox.h>
#include <wx/spinctrl.h>

namespace Slic3r { namespace GUI {

/// Dialog for pressure advance (PA) calibration using a chevron pattern.
/// Generates nested V-shaped chevrons inside a rectangular frame.  Each
/// layer is printed with a different PA value — the user inspects which
/// layer produces the sharpest corners at the chevron tips.
class CalibrationPADialog : public wxDialog
{
public:
    CalibrationPADialog(wxWindow* parent);

    double get_start_pa() const;
    double get_end_pa()   const;
    double get_pa_step()  const;

    /// Generate the chevron STL, load it onto the bed, and set per-layer
    /// PA commands (M572 S) via custom G-code.
    void generate_and_load();

private:
    wxSpinCtrlDouble* m_start_pa{nullptr};
    wxSpinCtrlDouble* m_end_pa{nullptr};
    wxSpinCtrlDouble* m_pa_step{nullptr};
    wxCheckBox*       m_brim{nullptr};
};

}} // namespace Slic3r::GUI

#endif // slic3r_CalibrationPADialog_hpp_
