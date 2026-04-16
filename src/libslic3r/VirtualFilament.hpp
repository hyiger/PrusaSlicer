#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace Slic3r {

// Represents a virtual "mixed" filament created by alternating layers of
// two or more physical filaments.  Virtual filament IDs are numbered
// starting at (num_physical + 1).
struct VirtualFilament
{
    enum DistributionMode : uint8_t {
        LayerCycle = 0,
        Simple     = 2
    };

    // 1-based physical filament IDs that are combined.
    unsigned int component_a = 1;
    unsigned int component_b = 2;

    // Persistent row identity – keeps painted virtual-tool assignments
    // stable even when the visible list is rebuilt.
    uint64_t stable_id = 0;

    // Layer-alternation ratio.  ratio_a=2, ratio_b=1  =>  A,A,B,A,A,B,...
    int ratio_a = 1;
    int ratio_b = 1;

    // Blend percentage of component B in [0..100].
    int mix_b_percent = 50;

    // Optional manual repeating pattern.  Tokens: '1'=>component_a,
    // '2'=>component_b, '3'..'9'=>direct physical IDs (1-based).
    std::string manual_pattern;

    // How this row is distributed.
    int distribution_mode = int(Simple);

    // Whether this virtual filament is available for assignment.
    bool enabled    = true;
    bool deleted    = false;
    bool custom     = false;
    bool origin_auto = false;

    // Computed display colour as "#RRGGBB".
    std::string display_color;

    bool operator==(const VirtualFilament &rhs) const;
    bool operator!=(const VirtualFilament &rhs) const { return !(*this == rhs); }
};

// ---------------------------------------------------------------------------
// VirtualFilamentManager
//
// Owns the list of virtual filaments and provides helpers used by the
// slicing pipeline to resolve virtual IDs back to physical extruders.
// ---------------------------------------------------------------------------
class VirtualFilamentManager
{
public:
    VirtualFilamentManager() = default;

    // ---- Auto-generation ------------------------------------------------

    // Rebuild the list from physical filament colours.
    // Generates all C(N,2) pairwise combinations, preserving prior state.
    void auto_generate(const std::vector<std::string> &filament_colours);

    // Remove a physical filament (1-based).
    void remove_physical_filament(unsigned int deleted_filament_id);

    // Add a user-created virtual filament.
    void add_custom(unsigned int component_a,
                    unsigned int component_b,
                    int          mix_b_percent,
                    const std::vector<std::string> &filament_colours);

    void clear_custom_entries();

    // Recompute cadence ratios from gradient settings.
    void apply_gradient_settings(int gradient_mode, float lower_bound,
                                 float upper_bound, bool advanced_dithering = false);

    // Serialization for project files.
    std::string serialize() const;
    void        deserialize(const std::string &serialized,
                            const std::vector<std::string> &filament_colours);

    // Normalize a manual pattern string.  Returns "" if invalid.
    static std::string normalize_manual_pattern(const std::string &pattern);
    static int         mix_percent_from_manual_pattern(const std::string &pattern);

    // ---- Queries --------------------------------------------------------

    bool is_virtual(unsigned int filament_id, size_t num_physical) const
    {
        return virtual_index_from_id(filament_id, num_physical) >= 0;
    }

    // Resolve a virtual filament ID to a physical extruder (1-based) for
    // the given layer.  Returns filament_id unchanged if not virtual.
    unsigned int resolve(unsigned int filament_id,
                         size_t       num_physical,
                         int          layer_index,
                         float        layer_print_z = 0.f,
                         float        layer_height  = 0.f) const;

    // Map virtual filament ID to index into m_virtuals.
    // Returns -1 if not a virtual filament.
    int virtual_index_from_id(unsigned int filament_id, size_t num_physical) const;

    const VirtualFilament *filament_from_id(unsigned int filament_id,
                                            size_t num_physical) const;

    // Blend two colours using perceptual pigment mixing.
    static std::string blend_color(const std::string &color_a,
                                   const std::string &color_b,
                                   int ratio_a, int ratio_b);

    // Blend N colours with weights.
    static std::string blend_color_multi(
        const std::vector<std::pair<std::string, int>> &color_percents);

    // ---- Accessors ------------------------------------------------------

    const std::vector<VirtualFilament> &filaments() const { return m_virtuals; }
    std::vector<VirtualFilament>       &filaments()       { return m_virtuals; }

    size_t enabled_count() const;
    size_t total_filaments(size_t num_physical) const { return num_physical + enabled_count(); }

    std::vector<std::string> display_colors() const;

private:
    void     refresh_display_colors(const std::vector<std::string> &filament_colours);
    uint64_t allocate_stable_id();
    uint64_t normalize_stable_id(uint64_t id);

    std::vector<VirtualFilament> m_virtuals;
    int                          m_gradient_mode      = 0;
    float                        m_height_lower_bound = 0.04f;
    float                        m_height_upper_bound = 0.16f;
    bool                         m_advanced_dithering = false;
    uint64_t                     m_next_stable_id     = 1;
};

} // namespace Slic3r
