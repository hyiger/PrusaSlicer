///|/ Copyright (c) 2025
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_CalibrationExtrusionDialog_hpp_
#define slic3r_CalibrationExtrusionDialog_hpp_

#include <wx/dialog.h>

namespace Slic3r { namespace GUI {

/// Simple dialog for the extrusion multiplier calibration test.
/// Generates a 40×40×40 mm cube in vase mode with classic perimeters,
/// no bottom layers, and a 5 mm brim.  The user prints the cube and
/// measures wall thickness to tune extrusion multiplier.
class CalibrationExtrusionDialog : public wxDialog
{
public:
    CalibrationExtrusionDialog(wxWindow* parent);

    /// Generate the cube STL, load it onto the bed, and configure
    /// print settings for the extrusion multiplier test.
    void generate_and_load();
};

}} // namespace Slic3r::GUI

#endif // slic3r_CalibrationExtrusionDialog_hpp_
