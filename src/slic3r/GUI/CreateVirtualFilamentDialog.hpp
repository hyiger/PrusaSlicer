#pragma once

#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <wx/panel.h>
#include <wx/button.h>

#include "libslic3r/VirtualFilament.hpp"

#include <vector>
#include <string>

namespace Slic3r {
namespace GUI {

// Modal dialog that lets the user type a target color (hex or CSS named)
// and shows a live preview of the closest achievable blend over the
// currently loaded physical filaments. On accept, caller can read the
// solved TargetColorSolution via solution().
//
// Two modes:
//  * Create (default) — title "Create Custom Virtual Filament", OK label
//    "Create", starts with "#FFA500".
//  * Edit — constructed via the edit-mode ctor with an initial name and
//    initial color (e.g. the existing row's display_color). Title and OK
//    label become "Edit Virtual Filament" / "Save".
class CreateVirtualFilamentDialog : public wxDialog
{
public:
    enum class Mode { Create, Edit };

    CreateVirtualFilamentDialog(wxWindow                       *parent,
                                const std::vector<std::string> &filament_colours,
                                int                             default_denominator = 12);

    // Edit-mode constructor. `initial_color` and `initial_name` pre-populate
    // the inputs.
    CreateVirtualFilamentDialog(wxWindow                       *parent,
                                const std::vector<std::string> &filament_colours,
                                const std::string              &initial_color,
                                const std::string              &initial_name,
                                int                             default_denominator = 12);

    // Valid only after ShowModal() returns wxID_OK.
    const VirtualFilamentManager::TargetColorSolution &solution() const { return m_solution; }

    // The raw color text the user entered (hex or name, normalized to hex).
    const std::string &resolved_target_hex() const { return m_target_hex; }

    // User-supplied name (may be empty). Trimmed on read.
    std::string entered_name() const;

private:
    void build(const std::string &initial_color, const std::string &initial_name);
    void on_input_changed();
    void on_pick_color();
    void recompute();

    // Refresh the preview swatches, ratio bars, and status line from
    // m_solution / m_target_hex.
    void refresh_preview();

    std::vector<std::string> m_filament_colours;
    int                      m_denominator = 12;
    Mode                     m_mode        = Mode::Create;

    wxTextCtrl  *m_input          = nullptr;
    wxTextCtrl  *m_name_input     = nullptr;
    wxButton    *m_pick_btn       = nullptr;
    wxPanel     *m_target_swatch  = nullptr;
    wxPanel     *m_achieved_swatch= nullptr;
    wxStaticText*m_ratio_text     = nullptr;
    wxStaticText*m_status_text    = nullptr;
    wxPanel     *m_ratio_bar_area = nullptr;
    wxBoxSizer  *m_ratio_bar_sizer= nullptr;
    wxButton    *m_ok_btn         = nullptr;

    VirtualFilamentManager::TargetColorSolution m_solution;
    std::string                                 m_target_hex;
};

} // namespace GUI
} // namespace Slic3r
