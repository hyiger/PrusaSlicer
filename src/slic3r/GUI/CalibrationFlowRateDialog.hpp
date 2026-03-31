///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationFlowRateDialog_hpp_
#define slic3r_CalibrationFlowRateDialog_hpp_

#include <wx/dialog.h>
#include <wx/checkbox.h>
#include <wx/spinctrl.h>

namespace Slic3r { namespace GUI {

/// Flow Rate calibration: generates 11 flat pads side by side, each with a
/// different extrusion width to simulate flow ratio from -5% to +5%.
/// Inspired by OrcaSlicer's YOLO Flow Calibration.
class CalibrationFlowRateDialog : public wxDialog
{
public:
    explicit CalibrationFlowRateDialog(wxWindow* parent);

private:
    wxSpinCtrl*       m_num_steps{nullptr};    // number of steps each side of center
    wxSpinCtrl*       m_step_pct{nullptr};     // step size in percent (e.g. 1 = 1%)
    wxSpinCtrl*       m_pad_width{nullptr};    // width of each pad (mm)
    wxSpinCtrl*       m_pad_depth{nullptr};    // depth of each pad (mm)
    wxSpinCtrlDouble* m_pad_height{nullptr};   // height of each pad (mm)
    wxCheckBox*       m_brim{nullptr};

    bool generate_and_load();
};

}} // namespace Slic3r::GUI

#endif // slic3r_CalibrationFlowRateDialog_hpp_
