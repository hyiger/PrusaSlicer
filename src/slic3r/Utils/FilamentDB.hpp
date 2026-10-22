///|/ Copyright (c) 2026
///|/
///|/ PrusaSlicer Calibration Tools
///|/
#ifndef slic3r_Utils_FilamentDB_hpp_
#define slic3r_Utils_FilamentDB_hpp_

#include <atomic>
#include <string>
#include <vector>
#include <functional>

namespace Slic3r {

class DynamicPrintConfig;
class PresetBundle;

// Represents a single filament preset fetched from a Filament DB REST API.
struct FilamentDBPreset {
    std::string name;
    std::string vendor;
    std::string type;
    // All key-value pairs from the INI section, ready to load into DynamicPrintConfig.
    std::vector<std::pair<std::string, std::string>> config_pairs;
};

// Fetch filament presets from a Filament DB REST API and load them into the
// PresetBundle's filament collection.
//
// The API endpoint is expected to return a PrusaSlicer-compatible INI config
// bundle with [filament:Name] sections (GET /api/filaments/prusaslicer).
//
// Presets loaded from the remote are marked as user presets and saved to the
// local filament preset directory so they persist across restarts. A fresh
// fetch replaces the previously synced presets.
//
// Returns the number of presets loaded, or -1 on error.
int load_filaments_from_filamentdb(
    PresetBundle &preset_bundle,
    const std::string &api_url,
    std::string &error_message
);

// Parse a PrusaSlicer INI config bundle string into FilamentDBPreset structs.
// This is a standalone function for testability.
std::vector<FilamentDBPreset> parse_filamentdb_bundle(const std::string &ini_content);

// Calibration data returned from FilamentDB for a specific nozzle diameter.
struct FilamentCalibration {
    double pressure_advance{-1};      // -1 = not set
    double max_volumetric_speed{-1};
    double extrusion_multiplier{-1};
    double retract_length{-1};
    double retract_speed{-1};
    double retract_lift{-1};
    std::string printer;              // printer the matched row is scoped to, "" for a shared row
    std::string nozzle_name;          // nozzle the matched row belongs to
    bool   found{false};
};

// One calibration lookup (GET /api/filaments/{id or name}/calibration).
struct FilamentCalibrationRequest {
    std::string filament_id;    // the preset's filamentdb_id; tried first (ObjectId addressing)
    std::string filament_name;  // used when there is no id, or this server doesn't know it
    double      nozzle_diameter{0};
    bool        high_flow{false};
    std::string nozzle_type;    // optional: narrows same-diameter nozzles (Brass vs Diamondback)
    std::string printer;        // optional: Filament DB printer id or name to scope the lookup to
};

// Fetch calibration data for a filament at a specific nozzle.
// Queries GET {api_url}/api/filaments/{id}/calibration?nozzle_diameter=..&high_flow=0|1
// [&nozzle_type=..][&printer=..], falling back to the filament name when the id is
// unknown to the server. Returns found=false if no match or error.
FilamentCalibration fetch_filament_calibration(
    const std::string &api_url,
    const FilamentCalibrationRequest &request
);

// A nozzle installed on a Filament DB printer.
struct FilamentDBNozzle {
    std::string id;
    std::string name;
    std::string type;
    double      diameter{0};
    bool        high_flow{false};
};

// A printer recorded in Filament DB (GET /api/printers), with its installed nozzles.
struct FilamentDBPrinter {
    std::string                   id;
    std::string                   name;
    std::vector<FilamentDBNozzle> nozzles;
};

// Fetch the live printers recorded in Filament DB. Returns false + error_message on
// a transport/HTTP/parse failure. Setting `cancel` aborts the request.
bool fetch_filamentdb_printers(
    const std::string &api_url,
    std::vector<FilamentDBPrinter> &out,
    std::string &error_message,
    const std::atomic<bool> *cancel = nullptr
);

// A process-wide copy of Filament DB's printer list, so lookups on the GUI thread
// never wait on the network. Refreshed at startup (with the preset bundle) and by
// the scan client's worker each time it connects.
// Fetch the list now (blocking) and keep it for `api_url`; on failure the previous
// copy stays. Returns whether the fetch succeeded. Setting `cancel` aborts it.
bool refresh_filamentdb_printer_cache(const std::string &api_url, const std::atomic<bool> *cancel = nullptr);
// The cached list for `api_url`; false when none has been fetched.
bool cached_filamentdb_printers(const std::string &api_url, std::vector<FilamentDBPrinter> &out);

// What the slicer knows about the printer it is slicing for, used to find the
// Filament DB printer that is the same machine.
struct SlicerPrinterIdentity {
    std::string              physical_printer; // selected physical printer's name, "" if none
    std::vector<std::string> preset_names;     // printer preset name, then its ancestors
};

// The identity of the printer selected in `bundle`.
SlicerPrinterIdentity slicer_printer_identity(const PresetBundle &bundle);

// Everything the printer-switch calibration lookup needs from the slicer.
struct FilamentCalibrationLookup {
    std::string           filament_name;   // the selected filament preset's name
    std::string           filament_id;     // its filamentdb_id
    std::string           nozzle_hint;     // its filamentdb_nozzle (per-nozzle presets only)
    double                nozzle_diameter{0}; // the printer's first extruder
    bool                  high_flow{false};
    SlicerPrinterIdentity printer;
};

// Look up the calibration to apply when the printer changes. Addresses the filament
// by its filamentdb_id, sends the printer's high-flow state and the matching Filament
// DB printer, and discards a row tuned on a different printer (a row scoped to it,
// or one on a nozzle installed on it). The printer list comes from the cache; without
// it nothing is read, since that check can't be made. A per-nozzle preset only reads its own
// nozzle's row, and nothing when the printer has a different nozzle, so a later
// sync-back (routed by the hint) can't write another nozzle's values onto it.
FilamentCalibration lookup_filament_calibration(
    const std::string &api_url,
    const FilamentCalibrationLookup &lookup
);

// Detailed result of a sync attempt.
struct FilamentDBSyncResult {
    bool        success = false;
    int         http_status = 0;       // last HTTP status seen
    std::string method;                // "POST", "PUT", "POST collection"
    std::string error_message;         // empty on success
    bool        existed_before = false; // true if pre-check GET returned 2xx
    bool        created_new = false;    // best-effort: true if first POST returned 201
                                        // or if pre-check was 404 and a later attempt 2xx'd

    // #36 Phase 2 — reconcile fields parsed from the server response body.
    // The #867 server resolves a name-addressed sync by a carried `filamentdb_id`
    // first; when that id resolves to a DIFFERENTLY-named filament it returns 409
    // `name_id_mismatch` WITHOUT mutating (a rename vs a copied/cloned id are
    // indistinguishable server-side). The GUI uses these to prompt the user.
    bool        name_id_mismatch = false; // true iff the server returned 409 name_id_mismatch
    std::string matched_by;               // "id" | "name" | "" — how the 200 resolved the record
    std::string matched_id;               // the filament _id the server resolved / conflicted on
    std::string matched_name;             // the stored name of that filament
    std::string sent_name;                // the preset name we sent (echoed back on a 409)

    // #36 Phase 2 — no-match interception. Set (with success=false) when a
    // name-addressed sync hit a status the upsert treats as a create-fallback
    // (404 not-found, or 405/501 no per-name route) AND create was suppressed
    // (allow_create=false). It's a decision point, not an error: the GUI prompts
    // Create-new vs Link-to-existing — the same cases the auto path would have
    // collection-created — instead of failing outright. error_message stays empty.
    bool        would_create = false;
};

// Sync a filament preset to the FilamentDB server with upsert semantics.
//
// Strategy (each attempt non-fatal; we fall through on 404/405/501):
//   1. POST /api/filaments/{name}            — canonical upsert
//   2. PUT  /api/filaments/{name}            — alternative upsert verb
//   3. POST /api/filaments  (name in body)   — REST collection-create
//
// When nozzle_diameter > 0 it is appended as ?nozzle_diameter=...&high_flow=0|1
// so the server can update the correct per-nozzle calibration entry.
//
// Also performs a pre-check GET to populate existed_before / created_new so
// the caller can show "Created new filament" vs "Updated filament" notifications.
//
// A per-nozzle preset (one carrying a filamentdb_nozzle hint) creates its base
// record ("<base>" for "<base> <hint>"); the hint routes its calibration.
//
// When allow_create is false, a "not found" (the server has no matching record)
// does NOT auto-create: the result comes back with would_create=true and
// success=false so the caller can prompt the user (Create new / Link to an
// existing filament). Default true preserves the original upsert behavior.
FilamentDBSyncResult sync_filament_to_filamentdb_detailed(
    const std::string &api_url,
    const std::string &preset_name,
    const DynamicPrintConfig &config,
    double nozzle_diameter = 0,
    bool high_flow = false,
    bool allow_create = true
);

// Backwards-compatible wrapper. Uses the detailed function under the hood and
// flattens the result to bool + error_message.
bool sync_filament_to_filamentdb(
    const std::string &api_url,
    const std::string &preset_name,
    const DynamicPrintConfig &config,
    std::string &error_message,
    double nozzle_diameter = 0,
    bool high_flow = false
);

// Re-sync a filament addressing it by its Filament DB ObjectId instead of by name
// (POST /api/filaments/{id}). This is the AUTHORITATIVE form — the server applies
// the update to exactly that record and a carried filamentdb_id can't redirect it.
// Used by the GUI "Update anyway" action after a 409 name_id_mismatch, where the
// user has confirmed the id is the intended target. No create-on-404 fallback: a
// missing id is a hard error (the record was deleted), not a reason to spawn one.
//
// When rename_only is true, the body carries ONLY the target name (plus the id,
// to satisfy the server's non-empty-config guard) — no calibration/structured
// keys and no nozzle query — so the server rewrites just the record's name and
// leaves its saved settings/calibration intact. This backs the reconcile
// "rename the DB record only" choice, distinct from a full push (rename_only=false).
FilamentDBSyncResult resync_filament_to_filamentdb_by_id(
    const std::string &api_url,
    const std::string &filament_id,
    const std::string &preset_name,
    const DynamicPrintConfig &config,
    double nozzle_diameter = 0,
    bool high_flow = false,
    bool rename_only = false
);

// A minimal filament identity from the DB, used to let the user link a preset to
// an existing record (#36 Phase 2 "Link to existing"). Sourced from the INI
// bundle (GET /api/filaments/prusaslicer), which emits filamentdb_id per section.
struct FilamentDBCandidate {
    std::string id;      // Filament DB ObjectId (from the section's filamentdb_id)
    std::string name;    // display name (the base name when per-nozzle sections collapse)
    std::string vendor;
    std::string type;
};

// List existing filaments a preset could be linked to, optionally narrowed to a
// vendor and/or type (server-side exact-match filters). Fetches the INI bundle
// (GET /api/filaments/prusaslicer) and keeps only records that carry a
// filamentdb_id (linking needs a stable id), de-duplicated by id. Returns true on
// a successful fetch (out may be empty); false + error_message on transport/HTTP
// error.
bool list_filamentdb_candidates(
    const std::string &api_url,
    const std::string &vendor,
    const std::string &type,
    std::vector<FilamentDBCandidate> &out,
    std::string &error_message
);

// Result of a spool remaining-filament check.
struct SpoolCheckResult {
    bool   ok{true};            // true if enough filament (or no data)
    bool   has_data{false};     // true if spool weight data was available
    std::string filament_name;
    double required_weight_g{0};
    std::string warning;        // human-readable warning if !ok
    struct SpoolInfo {
        std::string label;
        double remaining_weight_g{0};
        bool enough{true};
    };
    std::vector<SpoolInfo> spools;
};

// Check whether the filament has enough remaining material for a print.
// Queries GET {api_url}/api/filaments/{name}/spool-check?weight={weight_g}
// Returns a SpoolCheckResult (ok=true if enough or no data available).
SpoolCheckResult check_filament_spool(
    const std::string &api_url,
    const std::string &filament_name,
    double weight_g
);

namespace filamentdb_detail {

// Extract a single string from a typed config option, preserving any embedded
// commas/semicolons (e.g. a vendor like "Prusa Research, a.s.").
// Reads typed ConfigOptionString / ConfigOptionStrings pointers rather than
// opt_serialize, which would treat commas as list separators.
//   filament_vendor is coString  -> reads ConfigOptionString::value
//   filament_type   is coStrings -> reads ConfigOptionStrings::values.front()
// Returns "" if the key is missing or the option type is unexpected.
// Exposed for unit testing.
std::string config_first_string(const DynamicPrintConfig &cfg, const char *key);

// Extract a top-level JSON string value for `key` from a response `body`.
// Hand-rolled (no JSON dependency): handles \" \\ \n \t \r escapes and stops at
// the first unescaped quote. Returns "" if the key is absent or its value is
// null / non-string. Used to read the #867 sync response fields
// (matchedBy / matchedName / filamentId / error / sentName). Exposed for testing.
std::string extract_json_string(const std::string &body, const std::string &key);

// Convert a PrusaSlicer INI bundle into link candidates: keep only sections that
// carry a filamentdb_id, de-duplicated by id, keeping the shortest name (the base
// name, since per-nozzle exports only lengthen it). This is the pure core of
// list_filamentdb_candidates, split out so the id/dedupe/name logic is testable
// without a live server.
std::vector<FilamentDBCandidate> bundle_to_candidates(const std::string &ini_content);

// #872 per-nozzle fan-out: the nozzle a filamentdb_nozzle hint names. The server
// writes the hint as "<diameter> <type>[ HF]" ("0.4 Brass", "0.4 Other HF").
struct NozzleHint {
    double      diameter{0}; // 0 when the hint is empty or has no leading diameter
    std::string type;        // "" when the hint names no type
    bool        high_flow{false};
    bool        valid() const { return diameter > 0; }
};
NozzleHint parse_nozzle_hint(const std::string &hint);

// A config's filamentdb_nozzle hint, trimmed; "" when it has none.
std::string nozzle_hint(const DynamicPrintConfig &cfg);

// Whether a hint names the nozzle the printer has (same diameter and high-flow state).
bool hint_matches_nozzle(const NozzleHint &hint, double nozzle_diameter, bool high_flow);

// The Filament DB record name behind a per-nozzle preset: "<base> <hint>" → "<base>".
// Returns preset_name unchanged when it doesn't end in " <hint>".
std::string strip_nozzle_suffix(const std::string &preset_name, const std::string &hint);

// The calibration endpoint URL for `key` (an ObjectId or a filament name).
std::string calibration_url(const std::string &api_url, const std::string &key,
                            const FilamentCalibrationRequest &request);

// Parse a 200 response of the calibration endpoint (found=true when it parses).
FilamentCalibration parse_calibration_response(const std::string &body);

// Parse GET /api/printers into `out`; false when the body isn't a JSON array.
// Trashed printers and unpopulated nozzle refs are skipped.
bool parse_printers_json(const std::string &body, std::vector<FilamentDBPrinter> &out);

// The Filament DB printer that is the slicer's printer, or nullptr. In order: the
// physical printer's name (exact, then ignoring case and surrounding spaces); then
// each preset name: an equal name, else the longest printer name that starts it at
// a word boundary ("Prusa Core One INDX" for "Prusa CORE One INDX 8T HF0.4 nozzle").
// Two printers that tie are ambiguous and match nothing.
const FilamentDBPrinter *resolve_printer(const std::vector<FilamentDBPrinter> &printers,
                                         const SlicerPrinterIdentity &identity);

// The type of the one nozzle on `printer` with this diameter and high-flow state,
// "" when there is none or more than one.
std::string installed_nozzle_type(const FilamentDBPrinter &printer, double nozzle_diameter,
                                  bool high_flow);

// Whether a calibration row was tuned on a printer other than ours: it is scoped to
// another printer, or (when our printer is known) its nozzle is installed on another
// printer. `printer_name` is the name we asked for (the resolved printer's, or the
// raw slicer name we sent when nothing resolved).
// Replace the cached printer list for `api_url` (refresh_filamentdb_printer_cache's
// store; exposed for tests).
void store_printer_cache(const std::string &api_url, std::vector<FilamentDBPrinter> printers);

bool calibration_from_other_printer(const FilamentCalibration &cal,
                                    const std::vector<FilamentDBPrinter> &printers,
                                    const FilamentDBPrinter *our_printer,
                                    const std::string &printer_name);

// A filament preset, as the NFC scan picker sees it.
struct ScanPresetCandidate {
    std::string name;
    std::string filamentdb_id;
    std::string nozzle_hint;
    bool        compatible{true}; // with the active printer
};

// Which preset to select for a scanned Filament DB filament, as an index into
// `presets` (-1 for none). A filament calibrated on several nozzles has no preset
// under its own name, only "<name> <hint>" siblings sharing its id: pick the one
// for the printer's nozzle (diameter and high flow), narrowed by the type of the
// nozzle installed on the printer (asked for lazily, only when two siblings fit)
// and then by compatibility. Without a fitting sibling, fall back to a preset with
// the filament's own name, then to a compatible sibling, then to any sibling.
int pick_scanned_preset(const std::vector<ScanPresetCandidate> &presets,
                        const std::string &filament_id, const std::string &filament_name,
                        double nozzle_diameter, bool high_flow,
                        const std::function<std::string()> &installed_type);

} // namespace filamentdb_detail

} // namespace Slic3r

#endif // slic3r_Utils_FilamentDB_hpp_
