///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationRetractionDialog_hpp_
#define slic3r_CalibrationRetractionDialog_hpp_

#include <wx/dialog.h>
#include <wx/checkbox.h>
#include <wx/spinctrl.h>

namespace Slic3r { namespace GUI {

/// Retraction calibration: generates two cylindrical towers.
/// Retraction distance varies per layer group via M207 custom G-code.
class CalibrationRetractionDialog : public wxDialog
{
public:
    explicit CalibrationRetractionDialog(wxWindow* parent);

private:
    wxSpinCtrlDouble* m_start_retract{nullptr};
    wxSpinCtrlDouble* m_end_retract{nullptr};
    wxSpinCtrlDouble* m_retract_step{nullptr};
    wxSpinCtrl*       m_tower_height{nullptr};
    wxSpinCtrl*       m_tower_diameter{nullptr};
    wxSpinCtrl*       m_tower_spacing{nullptr};
    wxCheckBox*       m_brim{nullptr};

    bool generate_and_load();
};

}} // namespace Slic3r::GUI

#endif // slic3r_CalibrationRetractionDialog_hpp_
