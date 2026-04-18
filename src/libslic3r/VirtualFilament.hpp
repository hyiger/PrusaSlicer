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

    // Maximum number of consecutive same-component layers tolerated when
    // resolving. 0 disables the cap and falls back to the raw `ratio_a:ratio_b`
    // cycle. When positive, resolve() produces a spread-out sequence whose
    // longest contiguous run is <= this value, preserving the overall
    // `mix_b_percent` proportion.
    int local_z_max_sublayers = 0;

    // Optional 3+ component gradient distribution. When `gradient_component_ids`
    // has >= 3 entries, resolve() emits a weighted balanced sequence over these
    // physical filaments (1-based) using `gradient_component_weights` as the
    // per-component layer counts. Empty means: fall back to the 2-component
    // `component_a`/`component_b` cadence. This does not replace the blended
    // display color — solve_target_color continues to drive that via the
    // `manual_pattern`, which is independent of this field.
    std::vector<unsigned int> gradient_component_ids;
    std::vector<int>          gradient_component_weights;

    // Per-component XY surface-bias offsets in millimetres. The convention
    // (mirroring OrcaSlicer-FullSpectrum's canonical-signed-bias semantics)
    // is that exactly one of the two offsets is non-zero at a time — the
    // non-zero value represents the radial outward offset applied to that
    // component's extrusion paths, while the other component stays on the
    // nominal contour. Used to give the "dominant" colour a slight surface
    // bulge for more even perceived coverage. Zero means no bias.
    //
    // These fields are data-only for now: they serialize round-trip and are
    // queried via VirtualFilamentManager::component_surface_offset(), but
    // the G-code generator does not yet apply the offset to extrusion paths.
    // That wire-up is tracked as a follow-up.
    float component_a_surface_offset = 0.f;
    float component_b_surface_offset = 0.f;

    // Whether this virtual filament is available for assignment.
    bool enabled    = true;
    bool deleted    = false;
    bool custom     = false;
    bool origin_auto = false;

    // Computed display colour as "#RRGGBB".
    std::string display_color;

    // Optional user-supplied display name (e.g. "Teal", "Brand Orange").
    // Shown in the sidebar and painting gizmo when non-empty.
    std::string name;

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

    // Result of solving for a target color against a set of physical filaments.
    struct TargetColorSolution
    {
        // Per-physical-filament integer ratios (size == physical_colours.size()).
        std::vector<int> ratios;
        // Normalized pattern (e.g. "112223334") suitable for manual_pattern.
        std::string      pattern;
        // Blended color actually produced by the chosen ratios, as "#RRGGBB".
        std::string      achieved_color;
        // Target color (normalized hex).
        std::string      target_color;
        // Euclidean distance in 0..255 RGB between target and achieved.
        float            rgb_distance = 0.f;
    };

    // Parse a color string. Accepts "#RRGGBB", "#RGB", or a CSS named color
    // (lowercase). Returns the normalized "#RRGGBB" hex, or "" if invalid.
    static std::string parse_color_input(const std::string &input);

    // Find integer ratios (sum == max_denominator) over the given physical
    // colors that, when pigment-blended, come closest to target_hex.
    static TargetColorSolution solve_target_color(
        const std::string              &target_hex,
        const std::vector<std::string> &physical_colors,
        int                             max_denominator = 12);

    // Generate a repeating pattern string from per-physical ratios.
    // For ratios {2, 3, 4}, returns "112223333".
    static std::string pattern_from_ratios(const std::vector<int> &ratios);

    // Convenience: parse input (hex or named), solve for best ratios over the
    // given physical colors, and add a custom virtual filament with the
    // matching manual_pattern. Returns the index of the created virtual
    // (in m_virtuals), or -1 on parse failure / insufficient physicals.
    int add_custom_from_target_color(
        const std::string              &color_input,
        const std::vector<std::string> &filament_colours,
        int                             max_denominator = 12,
        const std::string              &name = std::string());

    // Replace the definition of an existing virtual filament at `index`
    // by solving for a new target color. Auto rows are converted to custom
    // on edit. `name` is applied verbatim (may be empty). Returns true on
    // success; false on parse failure, out-of-range index, or insufficient
    // physical filaments.
    bool update_from_target_color(
        size_t                           index,
        const std::string              &color_input,
        const std::string              &name,
        const std::vector<std::string> &filament_colours,
        int                             max_denominator = 12);

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

    // Resolve the XY surface-bias offset for the component that the virtual
    // filament would emit on the given layer. Returns 0 when:
    //   * `filament_id` is not virtual
    //   * the virtual row has no configured offset (both components are 0)
    //   * the row uses a multi-token manual_pattern (3+ components) where
    //     the A/B signed-bias convention doesn't map cleanly
    // Otherwise returns the signed outward offset in mm: positive means the
    // dominant component (the one with the configured offset) is shifted
    // outward, zero means the other component prints on the nominal contour.
    float component_surface_offset(unsigned int filament_id,
                                   size_t       num_physical,
                                   int          layer_index,
                                   float        layer_print_z = 0.f,
                                   float        layer_height  = 0.f) const;

    // The largest absolute surface-offset configured across any enabled
    // virtual filament, in mm. Useful for print-envelope / collision checks
    // when the G-code integration is wired up.
    float max_component_surface_offset() const;

    // Resolve a virtual filament ID to a physical extruder (1-based) for a
    // specific sub-layer *segment index*. Used by top-surface dithering:
    // within a single layer, consecutive segments alternate components by the
    // virtual filament's ratio, so the visible top face shows a fine per-
    // segment mix instead of a single-color stripe.
    //
    // The segment sequence is independent of the layer sequence — layer_index
    // only seeds the starting phase so successive dithered layers don't line
    // up identically. Returns filament_id unchanged if not virtual.
    unsigned int resolve_segment(unsigned int filament_id,
                                 size_t       num_physical,
                                 int          layer_index,
                                 int          segment_index) const;

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

    // Number of rows that currently reserve a virtual filament ID slot.
    // Disabled-but-not-deleted rows still reserve a slot so that painted
    // facets and object-level extruder assignments keep resolving to the
    // same virtual filament across enable/disable toggles.
    size_t reserved_count() const;

    size_t total_filaments(size_t num_physical) const { return num_physical + reserved_count(); }

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
