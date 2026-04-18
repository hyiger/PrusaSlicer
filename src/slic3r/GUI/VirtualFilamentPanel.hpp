#pragma once

#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/checkbox.h>
#include <wx/stattext.h>
#include <wx/scrolwin.h>
#include <wx/button.h>

#include <vector>
#include <string>
#include <functional>

namespace Slic3r {

class VirtualFilamentManager;

namespace GUI {

class Plater;

// A row in the virtual filament panel showing a blended color swatch,
// component names, ratio, and enable toggle.
struct VirtualFilamentRow {
    unsigned int component_a    = 0;
    unsigned int component_b    = 0;
    int          mix_b_percent  = 50;
    bool         enabled        = true;
    bool         deleted        = false;
    std::string  display_color;
    uint64_t     stable_id      = 0;
};

// Panel displaying auto-generated and custom virtual filaments.
// Shown in the sidebar below the filament preset combos.
class VirtualFilamentPanel : public wxPanel
{
public:
    VirtualFilamentPanel(wxWindow *parent, Plater *plater);

    // Rebuild the panel contents from the current virtual filament manager state.
    void rebuild(const VirtualFilamentManager &mgr,
                 const std::vector<std::string> &filament_colours,
                 size_t num_physical);

    // Show/hide the entire panel.
    void show_panel(bool show);

    // Callback when the user toggles a virtual filament's enabled state.
    // The caller should update the config and trigger reslice.
    std::function<void(size_t row_idx, bool enabled)> on_enable_changed;

    // Callback when the user clicks "+ Add custom…". The caller is
    // responsible for opening the Create dialog with the current filament
    // colors and applying the result to the print preset's
    // virtual_filament_definitions.
    std::function<void()> on_add_custom;

    // Callback when the user clicks the edit (pencil) button on a row.
    // `row_idx` indexes into the manager's filaments() vector that was
    // passed into the most recent rebuild() call (the full, unfiltered
    // index — same convention as on_enable_changed).
    std::function<void(size_t row_idx)> on_edit_row;

private:
    void clear_rows();
    wxPanel *create_color_swatch(wxWindow *parent, const std::string &hex_color, int size);

    Plater                   *m_plater = nullptr;
    wxBoxSizer               *m_rows_sizer = nullptr;
    wxStaticText             *m_title = nullptr;
    wxCheckBox               *m_enable_checkbox = nullptr;
    wxButton                 *m_add_button = nullptr;
    std::vector<wxSizer *>    m_row_sizers;
};

} // namespace GUI
} // namespace Slic3r
