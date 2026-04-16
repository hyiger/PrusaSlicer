// Virtual filament system for PrusaSlicer.
// Ported from OrcaSlicer-FullSpectrum's MixedFilament implementation.

#include "VirtualFilament.hpp"
#include "FilamentColorBlend.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace Slic3r {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

uint64_t canonical_pair_key(unsigned int a, unsigned int b)
{
    const unsigned int lo = std::min(a, b);
    const unsigned int hi = std::max(a, b);
    return (uint64_t(lo) << 32) | uint64_t(hi);
}

struct RGB { int r = 0, g = 0, b = 0; };

RGB parse_hex_color(const std::string &hex)
{
    RGB c;
    if (hex.size() >= 7 && hex[0] == '#') {
        try {
            c.r = std::stoi(hex.substr(1, 2), nullptr, 16);
            c.g = std::stoi(hex.substr(3, 2), nullptr, 16);
            c.b = std::stoi(hex.substr(5, 2), nullptr, 16);
        } catch (...) { c = {}; }
    }
    return c;
}

std::string rgb_to_hex(const RGB &c)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
    return std::string(buf);
}

int clamp_int(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

int safe_mod(int x, int m)
{
    if (m <= 0) return 0;
    int r = x % m;
    return (r < 0) ? (r + m) : r;
}

void normalize_ratio_pair(int &a, int &b)
{
    a = std::max(0, a);
    b = std::max(0, b);
    if (a == 0 && b == 0) { a = 1; return; }
    if (a > 0 && b > 0) {
        const int g = std::gcd(a, b);
        if (g > 1) { a /= g; b /= g; }
    }
}

void compute_gradient_ratios(VirtualFilament &vf, int gradient_mode,
                             float lower_bound, float upper_bound)
{
    if (gradient_mode == 1) {
        const int   mix_b = clamp_int(vf.mix_b_percent, 0, 100);
        const float pct_b = float(mix_b) / 100.f;
        const float pct_a = 1.f - pct_b;
        const float lo    = std::max(0.01f, lower_bound);
        const float hi    = std::max(lo, upper_bound);
        const float h_a   = lo + pct_a * (hi - lo);
        const float h_b   = lo + pct_b * (hi - lo);
        const float unit  = std::max(0.01f, std::min(h_a, h_b));
        vf.ratio_a = std::max(1, int(std::lround(h_a / unit)));
        vf.ratio_b = std::max(1, int(std::lround(h_b / unit)));
    } else {
        const int mix_b = clamp_int(vf.mix_b_percent, 0, 100);
        if (mix_b <= 0) { vf.ratio_a = 1; vf.ratio_b = 0; }
        else if (mix_b >= 100) { vf.ratio_a = 0; vf.ratio_b = 1; }
        else {
            const int pct_b = mix_b;
            const int pct_a = 100 - pct_b;
            const bool b_major = pct_b >= pct_a;
            const int major_pct = b_major ? pct_b : pct_a;
            const int minor_pct = b_major ? pct_a : pct_b;
            const int major_layers = std::max(1, int(std::lround(
                double(major_pct) / double(std::max(1, minor_pct)))));
            vf.ratio_a = b_major ? 1 : major_layers;
            vf.ratio_b = b_major ? major_layers : 1;
        }
    }
    normalize_ratio_pair(vf.ratio_a, vf.ratio_b);
}

int dithering_phase_step(int cycle)
{
    if (cycle <= 1) return 0;
    int step = cycle / 2 + 1;
    while (std::gcd(step, cycle) != 1) ++step;
    return step % cycle;
}

bool use_component_b_advanced_dither(int layer_index, int ratio_a, int ratio_b)
{
    ratio_a = std::max(0, ratio_a);
    ratio_b = std::max(0, ratio_b);
    const int cycle = ratio_a + ratio_b;
    if (cycle <= 0 || ratio_b <= 0) return false;
    if (ratio_a <= 0) return true;

    const int pos = safe_mod(layer_index, cycle);
    const int cycle_idx = (layer_index - pos) / cycle;
    // Use int64_t to prevent overflow with large layer indices.
    const int phase = safe_mod(int(int64_t(cycle_idx) * dithering_phase_step(cycle) % cycle), cycle);
    const int p = safe_mod(pos + phase, cycle);
    return ((p + 1) * ratio_b) / cycle > (p * ratio_b) / cycle;
}

bool is_pattern_separator(char c)
{
    return std::isspace((unsigned char)c) || c == '/' || c == '-' ||
           c == '_' || c == '|' || c == ':' || c == ';' || c == ',';
}

bool decode_pattern_step(char c, char &out)
{
    if (c >= '1' && c <= '9') { out = c; return true; }
    switch (std::tolower((unsigned char)c)) {
    case 'a': out = '1'; return true;
    case 'b': out = '2'; return true;
    default: return false;
    }
}

std::string flatten_manual_pattern(const std::string &pattern)
{
    std::string flat;
    flat.reserve(pattern.size());
    for (char c : pattern)
        if (c != ',') flat.push_back(c);
    return flat;
}

unsigned int physical_from_pattern_step(char token, const VirtualFilament &vf,
                                        size_t num_physical)
{
    if (token == '1') return vf.component_a;
    if (token == '2') return vf.component_b;
    if (token >= '3' && token <= '9') {
        unsigned int direct = unsigned(token - '0');
        if (direct >= 1 && direct <= num_physical) return direct;
    }
    return 0;
}

int mix_percent_from_normalized(const std::string &pattern)
{
    if (pattern.empty()) return 50;
    const int count_b = int(std::count(pattern.begin(), pattern.end(), '2'));
    return clamp_int(int(std::lround(100.0 * double(count_b) / double(pattern.size()))), 0, 100);
}

// Parse a serialized row definition.
bool parse_row(const std::string &row,
               unsigned int &a, unsigned int &b, uint64_t &stable_id,
               bool &enabled, bool &custom, bool &origin_auto,
               int &mix_b_percent, std::string &manual_pattern,
               int &distribution_mode, bool &deleted)
{
    auto trim = [](const std::string &s) {
        size_t lo = 0, hi = s.size();
        while (lo < hi && std::isspace((unsigned char)s[lo])) ++lo;
        while (hi > lo && std::isspace((unsigned char)s[hi - 1])) --hi;
        return s.substr(lo, hi - lo);
    };
    auto parse_int = [&trim](const std::string &tok, int &out) {
        std::string t = trim(tok);
        if (t.empty()) return false;
        try { size_t n = 0; out = std::stoi(t, &n); return n == t.size(); }
        catch (...) { return false; }
    };
    auto parse_u64 = [&trim](const std::string &tok, uint64_t &out) {
        std::string t = trim(tok);
        if (t.empty()) return false;
        try { size_t n = 0; out = uint64_t(std::stoull(t, &n)); return n == t.size(); }
        catch (...) { return false; }
    };

    std::vector<std::string> tokens;
    std::stringstream ss(row);
    std::string token;
    while (std::getline(ss, token, ',')) tokens.emplace_back(trim(token));

    if (tokens.size() < 4) return false;

    int values[5] = {0, 0, 1, 1, 50};
    if (tokens.size() == 4) {
        if (!parse_int(tokens[0], values[0]) || !parse_int(tokens[1], values[1]) ||
            !parse_int(tokens[2], values[2]) || !parse_int(tokens[3], values[4]))
            return false;
    } else {
        for (size_t i = 0; i < 5; ++i)
            if (!parse_int(tokens[i], values[i])) return false;
    }
    if (values[0] <= 0 || values[1] <= 0) return false;

    a = unsigned(values[0]);
    b = unsigned(values[1]);
    stable_id = 0;
    enabled = (values[2] != 0);
    custom = (tokens.size() == 4) ? true : (values[3] != 0);
    origin_auto = !custom;
    mix_b_percent = clamp_int(values[4], 0, 100);
    manual_pattern.clear();
    distribution_mode = int(VirtualFilament::Simple);
    deleted = false;

    // Parse metadata tokens (m=mode, d=deleted, o=origin_auto, u=stable_id)
    for (size_t i = 5; i < tokens.size(); ++i) {
        const std::string &tok = tokens[i];
        if (tok.empty()) continue;
        char prefix = char(std::tolower((unsigned char)tok[0]));
        if (prefix == 'm') {
            int mode = distribution_mode;
            if (parse_int(tok.substr(1), mode))
                distribution_mode = clamp_int(mode, 0, 2);
        } else if (prefix == 'd') {
            int d = 0;
            if (parse_int(tok.substr(1), d)) deleted = (d != 0);
        } else if (prefix == 'o') {
            int o = 0;
            if (parse_int(tok.substr(1), o)) origin_auto = (o != 0);
        } else if (prefix == 'u') {
            parse_u64(tok.substr(1), stable_id);
        } else if (prefix == 'g' || prefix == 'w' || prefix == 'z' ||
                   prefix == 'x') {
            // Skip gradient/offset tokens for now (Phase 6 features).
        } else {
            // Might be a manual pattern token.
            bool valid = true;
            for (char c : tok) {
                if (c < '1' || c > '9') { valid = false; break; }
            }
            if (valid && !tok.empty())
                manual_pattern = tok;
        }
    }

    return true;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// VirtualFilament
// ---------------------------------------------------------------------------

bool VirtualFilament::operator==(const VirtualFilament &rhs) const
{
    return component_a == rhs.component_a &&
           component_b == rhs.component_b &&
           stable_id   == rhs.stable_id   &&
           ratio_a     == rhs.ratio_a     &&
           ratio_b     == rhs.ratio_b     &&
           mix_b_percent == rhs.mix_b_percent &&
           manual_pattern == rhs.manual_pattern &&
           distribution_mode == rhs.distribution_mode &&
           enabled     == rhs.enabled &&
           deleted     == rhs.deleted &&
           custom      == rhs.custom &&
           origin_auto == rhs.origin_auto;
}

// ---------------------------------------------------------------------------
// VirtualFilamentManager
// ---------------------------------------------------------------------------

uint64_t VirtualFilamentManager::allocate_stable_id()
{
    return std::max<uint64_t>(1, m_next_stable_id++);
}

uint64_t VirtualFilamentManager::normalize_stable_id(uint64_t id)
{
    if (id == 0) return allocate_stable_id();
    if (id >= m_next_stable_id) m_next_stable_id = id + 1;
    return id;
}

void VirtualFilamentManager::auto_generate(const std::vector<std::string> &filament_colours)
{
    std::vector<VirtualFilament> old = std::move(m_virtuals);
    m_virtuals.clear();

    const size_t n = filament_colours.size();

    std::vector<VirtualFilament> custom_rows;
    custom_rows.reserve(old.size());
    std::unordered_map<uint64_t, const VirtualFilament *> old_auto;
    old_auto.reserve(old.size());

    for (const auto &prev : old) {
        if (!prev.custom) {
            old_auto.emplace(canonical_pair_key(prev.component_a, prev.component_b), &prev);
            continue;
        }
        if (prev.component_a == 0 || prev.component_b == 0 ||
            prev.component_a > n || prev.component_b > n ||
            prev.component_a == prev.component_b)
            continue;
        VirtualFilament c = prev;
        c.stable_id = normalize_stable_id(c.stable_id);
        custom_rows.push_back(std::move(c));
    }

    if (n >= 2) {
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = i + 1; j < n; ++j) {
                VirtualFilament vf;
                vf.component_a = unsigned(i + 1);
                vf.component_b = unsigned(j + 1);
                vf.ratio_a = 1;
                vf.ratio_b = 1;
                vf.mix_b_percent = 50;
                vf.enabled = true;
                vf.deleted = false;
                vf.custom = false;
                vf.origin_auto = true;

                auto it = old_auto.find(canonical_pair_key(vf.component_a, vf.component_b));
                if (it != old_auto.end()) {
                    vf.enabled = it->second->enabled;
                    vf.deleted = it->second->deleted;
                    vf.stable_id = it->second->stable_id;
                    if (vf.deleted) vf.enabled = false;
                }
                vf.stable_id = normalize_stable_id(vf.stable_id);
                m_virtuals.push_back(vf);
            }
        }
    }

    for (auto &c : custom_rows)
        m_virtuals.push_back(std::move(c));

    refresh_display_colors(filament_colours);
}

void VirtualFilamentManager::remove_physical_filament(unsigned int deleted_id)
{
    if (deleted_id == 0 || m_virtuals.empty()) return;

    std::vector<VirtualFilament> filtered;
    filtered.reserve(m_virtuals.size());
    for (VirtualFilament vf : m_virtuals) {
        if (vf.component_a == deleted_id || vf.component_b == deleted_id)
            continue;
        if (vf.component_a > deleted_id) --vf.component_a;
        if (vf.component_b > deleted_id) --vf.component_b;
        filtered.push_back(std::move(vf));
    }
    m_virtuals = std::move(filtered);
}

void VirtualFilamentManager::add_custom(unsigned int component_a,
                                        unsigned int component_b,
                                        int          mix_b_percent,
                                        const std::vector<std::string> &filament_colours)
{
    const size_t n = filament_colours.size();
    if (n < 2) return;

    component_a = std::max(1u, std::min(component_a, unsigned(n)));
    component_b = std::max(1u, std::min(component_b, unsigned(n)));
    if (component_a == component_b)
        component_b = (component_a == 1) ? 2 : 1;

    VirtualFilament vf;
    vf.component_a = component_a;
    vf.component_b = component_b;
    vf.stable_id = allocate_stable_id();
    vf.mix_b_percent = clamp_int(mix_b_percent, 0, 100);
    vf.enabled = true;
    vf.custom = true;
    vf.origin_auto = false;
    m_virtuals.push_back(std::move(vf));
    refresh_display_colors(filament_colours);
}

void VirtualFilamentManager::clear_custom_entries()
{
    m_virtuals.erase(
        std::remove_if(m_virtuals.begin(), m_virtuals.end(),
                       [](const VirtualFilament &vf) { return vf.custom; }),
        m_virtuals.end());
}

void VirtualFilamentManager::apply_gradient_settings(int gradient_mode,
                                                     float lower_bound,
                                                     float upper_bound,
                                                     bool advanced_dithering)
{
    m_gradient_mode = (gradient_mode != 0) ? 1 : 0;
    m_height_lower_bound = std::max(0.01f, lower_bound);
    m_height_upper_bound = std::max(m_height_lower_bound, upper_bound);
    m_advanced_dithering = advanced_dithering;

    for (auto &vf : m_virtuals) {
        if (!vf.custom) { vf.ratio_a = 1; vf.ratio_b = 1; continue; }
        compute_gradient_ratios(vf, m_gradient_mode, m_height_lower_bound, m_height_upper_bound);
    }
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

std::string VirtualFilamentManager::serialize() const
{
    std::ostringstream ss;
    bool first = true;
    for (const auto &vf : m_virtuals) {
        if (!first) ss << ';';
        first = false;
        ss << vf.component_a << ','
           << vf.component_b << ','
           << (vf.enabled ? 1 : 0) << ','
           << (vf.custom  ? 1 : 0) << ','
           << clamp_int(vf.mix_b_percent, 0, 100) << ','
           << "m" << clamp_int(vf.distribution_mode, 0, 2) << ','
           << "d" << (vf.deleted ? 1 : 0) << ','
           << "o" << (vf.origin_auto ? 1 : 0) << ','
           << "u" << vf.stable_id;
        std::string normalized = normalize_manual_pattern(vf.manual_pattern);
        if (!normalized.empty())
            ss << ',' << normalized;
    }
    return ss.str();
}

void VirtualFilamentManager::deserialize(const std::string &serialized,
                                         const std::vector<std::string> &filament_colours)
{
    const size_t n = filament_colours.size();
    if (serialized.empty() || n < 2) return;

    // Index existing auto rows by pair key.
    std::unordered_map<uint64_t, const VirtualFilament *> auto_by_pair;
    std::vector<const VirtualFilament *> auto_in_order;
    for (const auto &vf : m_virtuals) {
        if (!vf.custom) {
            auto_in_order.push_back(&vf);
            auto_by_pair.emplace(canonical_pair_key(vf.component_a, vf.component_b), &vf);
        }
    }

    std::vector<VirtualFilament> rebuilt;
    rebuilt.reserve(m_virtuals.size() + 8);
    std::unordered_set<uint64_t> consumed_pairs;
    std::unordered_set<uint64_t> used_ids;

    auto dedupe_id = [this, &used_ids](uint64_t id) -> uint64_t {
        id = normalize_stable_id(id);
        if (used_ids.insert(id).second) return id;
        uint64_t replacement = allocate_stable_id();
        used_ids.insert(replacement);
        return replacement;
    };

    std::stringstream all(serialized);
    std::string row;
    while (std::getline(all, row, ';')) {
        if (row.empty()) continue;

        unsigned int a = 0, b = 0;
        uint64_t sid = 0;
        bool en = true, cust = true, oa = false, del = false;
        int mix = 50, mode = int(VirtualFilament::Simple);
        std::string pattern;

        if (!parse_row(row, a, b, sid, en, cust, oa, mix, pattern, mode, del))
            continue;
        if (a == 0 || b == 0 || a > n || b > n || a == b)
            continue;

        if (!cust) {
            const uint64_t key = canonical_pair_key(a, b);
            if (consumed_pairs.count(key)) continue;
            auto it = auto_by_pair.find(key);
            if (it == auto_by_pair.end()) continue;

            VirtualFilament vf = *it->second;
            vf.component_a = std::min(a, b);
            vf.component_b = std::max(a, b);
            vf.stable_id = dedupe_id(sid != 0 ? sid : vf.stable_id);
            vf.enabled = en;
            vf.deleted = del;
            if (vf.deleted) vf.enabled = false;
            vf.manual_pattern = normalize_manual_pattern(pattern);
            vf.distribution_mode = clamp_int(mode, 0, 2);
            vf.mix_b_percent = vf.manual_pattern.empty() ? mix :
                mix_percent_from_normalized(vf.manual_pattern);
            vf.custom = false;
            vf.origin_auto = true;
            rebuilt.push_back(std::move(vf));
            consumed_pairs.insert(key);
            continue;
        }

        VirtualFilament vf;
        vf.component_a = a;
        vf.component_b = b;
        vf.stable_id = dedupe_id(sid);
        vf.mix_b_percent = mix;
        vf.manual_pattern = normalize_manual_pattern(pattern);
        if (!vf.manual_pattern.empty())
            vf.mix_b_percent = mix_percent_from_normalized(vf.manual_pattern);
        vf.distribution_mode = clamp_int(mode, 0, 2);
        vf.enabled = en;
        vf.deleted = del;
        if (vf.deleted) vf.enabled = false;
        vf.custom = cust;
        vf.origin_auto = oa;
        rebuilt.push_back(std::move(vf));
    }

    // Append any auto rows not present in serialized data.
    for (const auto *auto_vf : auto_in_order) {
        if (!auto_vf) continue;
        const uint64_t key = canonical_pair_key(auto_vf->component_a, auto_vf->component_b);
        if (consumed_pairs.count(key)) continue;
        VirtualFilament vf = *auto_vf;
        const unsigned int lo = std::min(vf.component_a, vf.component_b);
        const unsigned int hi = std::max(vf.component_a, vf.component_b);
        vf.component_a = lo;
        vf.component_b = hi;
        vf.stable_id = dedupe_id(vf.stable_id);
        vf.custom = false;
        vf.origin_auto = true;
        rebuilt.push_back(std::move(vf));
    }

    m_virtuals = std::move(rebuilt);
    refresh_display_colors(filament_colours);
}

// ---------------------------------------------------------------------------
// Pattern normalization
// ---------------------------------------------------------------------------

std::string VirtualFilamentManager::normalize_manual_pattern(const std::string &pattern)
{
    std::string normalized;
    normalized.reserve(pattern.size());
    for (char c : pattern) {
        char step = '\0';
        if (decode_pattern_step(c, step)) {
            normalized.push_back(step);
            continue;
        }
        if (is_pattern_separator(c)) continue;
        return {}; // invalid character
    }
    return normalized;
}

int VirtualFilamentManager::mix_percent_from_manual_pattern(const std::string &pattern)
{
    return mix_percent_from_normalized(normalize_manual_pattern(pattern));
}

// ---------------------------------------------------------------------------
// Resolution
// ---------------------------------------------------------------------------

unsigned int VirtualFilamentManager::resolve(unsigned int filament_id,
                                             size_t       num_physical,
                                             int          layer_index,
                                             float        /*layer_print_z*/,
                                             float        /*layer_height*/) const
{
    const int idx = virtual_index_from_id(filament_id, num_physical);
    if (idx < 0) return filament_id;

    const VirtualFilament &vf = m_virtuals[size_t(idx)];

    // Manual pattern takes precedence.
    if (!vf.manual_pattern.empty()) {
        const std::string flat = flatten_manual_pattern(vf.manual_pattern);
        if (!flat.empty()) {
            const int pos = safe_mod(layer_index, int(flat.size()));
            unsigned int resolved = physical_from_pattern_step(flat[size_t(pos)], vf, num_physical);
            if (resolved >= 1 && resolved <= num_physical)
                return resolved;
        }
        return vf.component_a;
    }

    const int cycle = vf.ratio_a + vf.ratio_b;
    if (cycle <= 0) return vf.component_a;

    // Advanced dithering for more even distribution.
    if (m_gradient_mode == 0 && m_advanced_dithering && vf.custom)
        return use_component_b_advanced_dither(layer_index, vf.ratio_a, vf.ratio_b)
                   ? vf.component_b : vf.component_a;

    // Simple layer-cycle cadence.
    const int pos = safe_mod(layer_index, cycle);
    return (pos < vf.ratio_a) ? vf.component_a : vf.component_b;
}

int VirtualFilamentManager::virtual_index_from_id(unsigned int filament_id,
                                                  size_t num_physical) const
{
    if (filament_id <= num_physical) return -1;

    const size_t target = size_t(filament_id - num_physical - 1);
    size_t enabled_seen = 0;
    for (size_t i = 0; i < m_virtuals.size(); ++i) {
        if (!m_virtuals[i].enabled || m_virtuals[i].deleted) continue;
        if (enabled_seen == target) return int(i);
        ++enabled_seen;
    }
    return -1;
}

const VirtualFilament *VirtualFilamentManager::filament_from_id(unsigned int filament_id,
                                                                size_t num_physical) const
{
    const int idx = virtual_index_from_id(filament_id, num_physical);
    return idx >= 0 ? &m_virtuals[size_t(idx)] : nullptr;
}

// ---------------------------------------------------------------------------
// Color blending
// ---------------------------------------------------------------------------

std::string VirtualFilamentManager::blend_color(const std::string &color_a,
                                                const std::string &color_b,
                                                int ratio_a, int ratio_b)
{
    const int safe_a = std::max(0, ratio_a);
    const int safe_b = std::max(0, ratio_b);
    const int total  = safe_a + safe_b;
    const float t    = (total > 0) ? (float(safe_b) / float(total)) : 0.5f;

    const RGB a = parse_hex_color(color_a);
    const RGB b = parse_hex_color(color_b);

    unsigned char out_r, out_g, out_b;
    filament_color_blend(
        (unsigned char)a.r, (unsigned char)a.g, (unsigned char)a.b,
        (unsigned char)b.r, (unsigned char)b.g, (unsigned char)b.b,
        t, &out_r, &out_g, &out_b);

    return rgb_to_hex({int(out_r), int(out_g), int(out_b)});
}

std::string VirtualFilamentManager::blend_color_multi(
    const std::vector<std::pair<std::string, int>> &color_percents)
{
    if (color_percents.empty()) return "#000000";
    if (color_percents.size() == 1) return color_percents.front().first;

    struct WC { RGB color; int pct; };
    std::vector<WC> colors;
    int total_pct = 0;
    for (const auto &[hex, pct] : color_percents) {
        if (pct <= 0) continue;
        colors.push_back({parse_hex_color(hex), pct});
        total_pct += pct;
    }
    if (colors.empty() || total_pct <= 0) return "#000000";

    unsigned char r = (unsigned char)colors[0].color.r;
    unsigned char g = (unsigned char)colors[0].color.g;
    unsigned char b = (unsigned char)colors[0].color.b;
    int acc_pct = colors[0].pct;

    for (size_t i = 1; i < colors.size(); ++i) {
        const int new_total = acc_pct + colors[i].pct;
        if (new_total <= 0) continue;
        const float t = float(colors[i].pct) / float(new_total);
        filament_color_blend(
            r, g, b,
            (unsigned char)colors[i].color.r,
            (unsigned char)colors[i].color.g,
            (unsigned char)colors[i].color.b,
            t, &r, &g, &b);
        acc_pct = new_total;
    }

    return rgb_to_hex({int(r), int(g), int(b)});
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

size_t VirtualFilamentManager::enabled_count() const
{
    size_t count = 0;
    for (const auto &vf : m_virtuals)
        if (vf.enabled && !vf.deleted) ++count;
    return count;
}

std::vector<std::string> VirtualFilamentManager::display_colors() const
{
    std::vector<std::string> colors;
    for (const auto &vf : m_virtuals)
        if (vf.enabled && !vf.deleted)
            colors.push_back(vf.display_color);
    return colors;
}

void VirtualFilamentManager::refresh_display_colors(
    const std::vector<std::string> &filament_colours)
{
    for (auto &vf : m_virtuals) {
        if (vf.component_a == 0 || vf.component_b == 0 ||
            vf.component_a > filament_colours.size() ||
            vf.component_b > filament_colours.size()) {
            vf.display_color = "#26A69A";
            continue;
        }
        const int ratio_b = clamp_int(vf.mix_b_percent, 0, 100);
        const int ratio_a = std::max(0, 100 - ratio_b);
        vf.display_color = blend_color(
            filament_colours[vf.component_a - 1],
            filament_colours[vf.component_b - 1],
            ratio_a, ratio_b);
    }
}

} // namespace Slic3r
