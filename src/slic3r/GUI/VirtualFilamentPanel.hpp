#pragma once

#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/checkbox.h>
#include <wx/stattext.h>
#include <wx/scrolwin.h>

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

private:
    void clear_rows();
    wxPanel *create_color_swatch(wxWindow *parent, const std::string &hex_color, int size);

    Plater                   *m_plater = nullptr;
    wxBoxSizer               *m_rows_sizer = nullptr;
    wxStaticText             *m_title = nullptr;
    wxCheckBox               *m_enable_checkbox = nullptr;
    std::vector<wxSizer *>    m_row_sizers;
};

} // namespace GUI
} // namespace Slic3r
