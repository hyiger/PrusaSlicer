# CLAUDE.md

## Build & Run

PrusaSlicer uses CMake presets. Build deps and the app in one step:

```bash
cmake --preset default -DPrusaSlicer_BUILD_DEPS=ON
cmake --build build-default -j$(sysctl -n hw.ncpu)
```

The binary is at `build-default/src/prusa-slicer`.

### Available Presets

| Preset | Description |
|--------|-------------|
| `default` | Static linking, Release, GTK3, PCH enabled |
| `no-occt` | Same as default, no STEP file support |
| `mac_universal_arm` | macOS arm64 slice for universal binary (no OCCT) |
| `mac_universal_x86` | macOS x86_64 slice for universal binary (no OCCT) |
| `shareddeps` | Dynamic linking, uses system libraries |

Deps presets (in `deps/CMakePresets.json`): `default`, `no-occt`, `mac_universal_x86`, `mac_universal_arm`.

### Key Build Options

```
-DSLIC3R_GUI=OFF                 # CLI only, no GUI
-DSLIC3R_ENABLE_FORMAT_STEP=OFF  # Disable STEP file support
-DSLIC3R_PCH=OFF                 # Disable precompiled headers
-DSLIC3R_ASAN=ON                 # AddressSanitizer
-DSLIC3R_UBSAN=ON                # UndefinedBehaviorSanitizer
-DSLIC3R_STATIC=OFF              # Dynamic linking (default ON on macOS/Windows)
-DSLIC3R_GTK=2                   # GTK version for wxWidgets (Linux, default 2)
-DSLIC3R_BUILD_SANDBOXES=ON      # Dev sandboxes
-DSLIC3R_BUILD_TESTS=OFF         # Disable tests
-DCMAKE_BUILD_TYPE=Debug         # Debug build (enables asserts in tests)
```

### Homebrew Conflicts

Homebrew `boost`, `eigen`, or `glew` can conflict with the deps build. Uninstall them if you hit cmake errors:

```bash
brew uninstall boost eigen glew
```

## Test

Test framework: Catch2 3.8+

```bash
cd build-default
make test                                    # all tests
./tests/libslic3r/libslic3r_tests           # core library tests
./tests/fff_print/fff_print_tests           # FFF print tests
./tests/sla_print/sla_print_tests           # SLA print tests
./tests/arrange/arrange_tests               # arrangement tests
./tests/thumbnails/thumbnails_tests         # thumbnail tests
./tests/slic3rutils/slic3rutils_tests       # utility tests (GUI only)
```

For full assert coverage, build with `-DCMAKE_BUILD_TYPE=Debug`.

Test data lives in `tests/data/`. Shared test data defined in `tests/data/prusaparts.cpp`.

The `tests/cpp17/` suite is `EXCLUDE_FROM_ALL` (not built by default).

## Architecture

### Slicing Pipeline

```
Input files (STL/3MF/AMF/OBJ/STEP)
  ↓
Model (ModelObject → ModelVolume → ModelInstance)
  ↓
Print (PrintObject → PrintRegion → PrintInstance)
  ↓
Per-object slicing steps (enum PrintObjectStep):
  posSlice             → Mesh → 2D cross-sections (TriangleMeshSlicer)
  posPerimeters        → Shell generation (Arachne or Classic engine)
  posPrepareInfill     → Top/bottom/internal surface detection
  posInfill            → Fill pattern generation
  posIroning           → Optional top surface smoothing
  posSupportSpotsSearch → Support point detection
  posSupportMaterial   → Support structure generation (grid/tree/snug)
  posEstimateCurledExtrusions → Warp prediction
  posCalculateOverhangingPerimeters → Overhang detection
  ↓
Print-level steps (enum PrintStep):
  psWipeTower          → Tool change / wipe tower planning
  psAlertWhenSupportsNeeded → Support requirement warning
  psSkirtBrim          → Skirt and brim generation
  psGCodeExport        → G-code file output
  ↓
GCodeGenerator::do_export
  → Order extrusions per layer
  → Travel moves (retract, avoid perimeters)
  → Extrusion moves (perimeters → infill → ironing)
  → Post-processors: CoolingBuffer, PressureEqualizer, SpiralVase, FindReplace
  ↓
Output .gcode / .bgcode
```

### Entry Points

- **`src/PrusaSlicer.cpp`** — main entry, dispatches to `Slic3r::CLI::run()`
- **`src/CLI/Run.cpp`** — CLI orchestrator: `setup()` → `load_print_data()` → `process_transform()` → `process_actions()` → optionally `start_gui_with_params()`
- **`src/CLI/Setup.cpp`** — argument parsing (5 config types: input, overrides, transform, misc, actions)
- **`src/CLI/LoadPrintData.cpp`** — model/config loading and merging
- **`src/CLI/ProcessActions.cpp`** — slicing and export logic

### Key Data Structures

| Type | File | Purpose |
|------|------|---------|
| `Model` | `Model.hpp` | Top-level container for all printable objects |
| `ModelObject` | `Model.hpp` | Single 3D object with volumes and instances |
| `ModelVolume` | `Model.hpp` | Individual mesh with config overrides |
| `ModelInstance` | `Model.hpp` | Positioned/rotated copy of a ModelObject |
| `Print` | `Print.hpp` | Main slicing orchestrator |
| `PrintObject` | `Print.hpp` | Per-object slicing state (layers, support, extrusions) |
| `PrintRegion` | `Print.hpp` | Shared config for volumes with same extrusion settings |
| `Layer` | `Layer.hpp` | 2D cross-section at a specific Z height |
| `LayerRegion` | `Layer.hpp` | Layer slice for a specific PrintRegion |
| `LayerSlice` | `Layer.hpp` | Connected island of a layer |
| `GCodeGenerator` | `GCode.hpp` | G-code exporter |
| `ExtrusionEntity` | — | Individual extrusion path with role |
| `ExPolygon` | — | Polygon with holes (primary 2D geometry type) |
| `Point` | — | 2D integer coordinates (scaled microns) |
| `Surface` | — | Connected region with role (top/bottom/internal) |

### Configuration System

Three-level architecture:

1. **`DynamicPrintConfig`** (`PrintConfig.hpp`, inherits from `DynamicConfig` in `Config.hpp`) — runtime key-value pairs, used by GUI, supports arbitrary merging
2. **`StaticPrintConfig`** (`PrintConfig.hpp`) — compile-time typed config hierarchy:
   ```
   FullPrintConfig
     ├── PrintObjectConfig   (per-object: support, infill, layer height)
     ├── PrintRegionConfig   (per-region: perimeter width, extrusion multiplier)
     └── PrintConfig         (global: printer, material, speed)
         └── GCodeConfig     (post-processor config)
   ```
3. **`AppConfig`** (`AppConfig.hpp`) — user preferences, window state, paths

Config inheritance: Model object config → material/print profiles → layer range overrides. PrintRegions created by config hash matching (volumes with same settings share a region).

### Threading Model

- **Library:** Intel TBB (`oneapi::tbb`)
- **Execution policies:** `ExecutionSeq.hpp` (sequential) and `ExecutionTBB.hpp` (parallel), selected via `Execution.hpp`
- **Background processing:** `Print::process()` runs on worker thread pool
- **State machine:** Each `PrintStep`/`PrintObjectStep` has `StateWithTimeStamp` (Fresh → Started → Done/Canceled → Invalidated)
- **Thread safety:** `PrintBase.hpp` uses `std::mutex` for state queries; main thread has unguarded access when background stopped
- **Cancellation:** Callback-based, checked between steps

### Key Directories

| Directory | Purpose |
|-----------|---------|
| `src/libslic3r/` | Core slicing library |
| `src/libslic3r/GCode/` | G-code generation, cooling buffer, conflict checker |
| `src/libslic3r/Fill/` | Infill patterns (30+: honeycomb, gyroid, lightning, adaptive cubic, etc.) |
| `src/libslic3r/Support/` | Support material (grid, tree, snug) |
| `src/libslic3r/Arachne/` | Modern perimeter generator (edge-based, variable width) |
| `src/libslic3r/SLA/` | Resin printer pipeline (separate from FFF) |
| `src/libslic3r/Format/` | File format readers (STL, 3MF, AMF, OBJ, SL1) |
| `src/libslic3r/Geometry/` | Geometric algorithms (normals, intersections, arc welding) |
| `src/libslic3r/Algorithm/` | Convex hull, offset, medial axis, path planning |
| `src/libslic3r/BranchingTree/` | Tree support generation |
| `src/libslic3r/CSGMesh/` | Constructive solid geometry |
| `src/libslic3r/Optimize/` | Path optimization, travel planning |
| `src/libslic3r/Execution/` | TBB/sequential execution policies |
| `src/slic3r/GUI/` | wxWidgets GUI application |
| `src/slic3r/Config/` | Configuration/preset management |
| `src/slic3r/Utils/` | GUI utility functions |
| `src/CLI/` | Command-line interface |
| `src/libvgcode/` | G-code viewer component |
| `src/occt_wrapper/` | STEP file support (OpenCASCADE, optional) |
| `src/clipper/` | Polygon clipping library |
| `src/slic3r-arrange/` | Object arrangement/nesting |
| `src/libseqarrange/` | Sequential arrangement |
| `src/platform/` | Platform-specific code (msw, unix, osx) |
| `deps/` | External dependency build scripts (28 packages) |
| `bundled_deps/` | Vendored libraries (see below) |
| `resources/profiles/` | Printer profiles (~106) |
| `resources/localization/` | Translations (~27 languages) |
| `resources/icons/` | UI icons |
| `tests/` | Unit tests (Catch2) |
| `cmake/modules/` | Custom CMake find modules |
| `build-utils/` | Encoding check utilities (UTF-8 verification) |

### Bundled Dependencies (`bundled_deps/`)

admesh, agg, ankerl, avrdude, fast_float, glu-libtess, hidapi, hints, imgui, int128, libigl, libnest2d, localesutils, miniz, qoi, semver, stb_dxt, stb_image, tcbspan

## Conventions

- C++17 (`CMAKE_CXX_STANDARD 17`)
- `.clang-format` enforced: 100-char column limit, 4-space indent, no tabs
- Pointer alignment: right (`*ptr`)
- Brace style: opening brace on new line for classes/structs; same line for functions/namespaces
- Constructor initializers: one per line, break before comma
- Namespace indentation: none (compact)
- Include ordering: preserve organization; Qt/gtest/json at special priority
- Precompiled headers enabled by default
- `from __future__ import annotations` style not applicable (C++ project)
- Compiler flags: `-fsigned-char -Wall -Werror=return-type` (GCC/Clang), `/MP -bigobj` (MSVC)

## Dependencies

Built automatically via `PrusaSlicer_BUILD_DEPS=ON` (28 packages in `deps/+<name>/`):

| Library | Purpose |
|---------|---------|
| **wxWidgets 3.2+** | UI framework |
| **Boost 1.83+** | system, filesystem, thread, log, locale, regex, chrono, atomic, date_time, iostreams, nowide |
| **Eigen3 3.3.7+** | Linear algebra |
| **TBB** | Intel Threading Building Blocks (parallel execution) |
| **CGAL** | Computational geometry |
| **OpenVDB 5.0+** | Sparse volumetric data |
| **OCCT** | STEP file support (optional) |
| **NLopt 1.4+** | Nonlinear optimization |
| **libcurl** | HTTP client |
| **OpenSSL** | Cryptography |
| **libBGCode** | Binary G-code format |
| **Catch2 3.8+** | Unit testing |
| **Cereal** | Serialization |
| **GMP / MPFR** | Arbitrary precision math (for CGAL) |
| **Blosc** | Compression (for OpenVDB) |
| **OpenEXR** | HDR image format (for OpenVDB) |
| **GLEW** | OpenGL extension loading |
| **PNG / JPEG / EXPAT / ZLIB** | Image and data formats |
| **Qhull** | Convex hull computation |
| **NanoSVG** | SVG parsing |
| **z3** | SMT solver |
| **heatshrink** | Compression |
| **json** | JSON parsing |
| **OpenCSG** | Constructive solid geometry rendering |

System-provided on macOS: OpenGL, ZLIB, CURL.

## CI

GitHub Actions workflows in `.github/workflows/` (self-contained, no external action dependencies):

| Workflow | Trigger | Purpose |
|----------|---------|---------|
| `build-macos.yml` | push to master, release, dispatch | macOS build + DMG |
| `build-linux.yml` | push to master, release, dispatch | Linux build |
| `build-windows.yml` | push to master, release, dispatch | Windows build + installer |

## Localization

gettext-based workflow with custom CMake targets:
- `gettext_make_pot` — extract strings
- `gettext_merge_community_po_with_pot` — merge community translations
- `gettext_concat_wx_po_with_po` — merge wxWidgets translations
- `gettext_po_to_mo` — compile .po to .mo
- Languages: cs, de, es, fr, it, ja, pl (and more in resources)

## Calibration Fork Additions

This fork adds a **Calibration** menu with built-in calibration tools:

| Tool | Geometry | G-code |
|------|----------|--------|
| Temperature Tower | Multi-tier tower with overhangs, holes, cones, text labels | Per-layer `M104` |
| Flow Rate (YOLO) | 11 rounded-rect pads with label tabs, Archimedean Chords spiral | Per-object extrusion multiplier |
| Pressure Advance | Chevron pattern tower | Per-layer `M572 S` / `M900 K` / `SET_PRESSURE_ADVANCE` |
| Retraction | Two cylindrical towers on base plate (seam=rear) | Per-layer `M207 S` (firmware retraction) |
| Max FlowRate | Serpentine E-shape specimen | Per-layer `M220 S` speed override |
| Extrusion Multiplier | 40×40×40mm vase-mode cube | Vase mode settings |
| Fan Speed | Two-column tower with shelves, wedges, cones, standalone cylinder | Per-layer `M106 S` |
| Dimensional Accuracy | XYZ cross gauge with through-holes and labels | None (measure after printing) |

**XY Skew Correction** — Native coordinate shear transform in `GCodeGenerator::point_to_gcode()`. Configured per-printer in Printer Settings → General → Skew Correction (Expert mode). Automatically disables arc fitting when active.

**Bed Mesh Visualization** — Renders a 3D heatmap of the printer's stored bed mesh directly on the build plate. Two entry points in the Calibration menu:

| Command | What it does |
|--------|--------------|
| **Fetch Bed Mesh** | Auto-detects a Prusa printer on USB serial, sends `M420 S1` + `M420 V1 T1`, parses the "Bed Topography Report for CSV:" block, displays the heatmap. <1s latency. Requires the mesh to already be stored on the printer. |
| **Probe Bed Mesh…** | Confirmation dialog → home (`G28`) → heat nozzle to 170 °C (`M104`/`M109`) → probe (`G29`) → cool → fetch (`M420 V1 T1`). Runs on a worker thread with a `wxProgressDialog` showing live phase / probe count. Typical duration: 3–5 minutes. |
| **Show Bed Mesh Overlay** | Checkable menu item that toggles overlay visibility. Handler uses `wxCommandEvent::IsChecked()` so the check state always matches actual visibility. |

Heatmap details:
- Diverging multi-stop color ramp (dark blue → blue → cyan → white → yellow → orange → red → dark red) so mid-magnitude points stay visible.
- Reference combo in the legend (Mean / Zero): Mean centers white on the mesh average (warp view), Zero centers on the nominal plane (absolute compensation view).
- Z exaggeration slider and absolute-Z labels on the color scale bar.
- Cross-platform USB port enumeration reuses the existing `scan_serial_ports_extended()` (matches "Original Prusa" friendly name / VID 0x2C99).

Dev/env hooks (all optional; fall through to the next source if unset):
- `PRUSASLICER_BED_MESH_CSV=/path/to/csv` — load a saved `M420` CSV instead of querying the printer. Same tab-separated format the firmware emits.
- `PRUSASLICER_BED_MESH_EXTENT="xmin,ymin,xmax,ymax"` — override the XY extent the grid spans. Default: bed bounds inset by 10 mm. Core One's firmware reports 2..248 / 3..217.
- `PRUSASLICER_BED_MESH_PORT=/dev/cu.usbmodem101` — force a specific serial device instead of auto-detecting.

Key files:
- `src/libslic3r/CalibrationModels.cpp/hpp` — geometry generators
- `src/slic3r/GUI/Calibration*Dialog.cpp/hpp` — dialog UIs
- `src/slic3r/GUI/MainFrame.cpp` — menu wiring
- `src/libslic3r/GCode.hpp` — skew transform in `point_to_gcode()`
- `src/slic3r/GUI/BedMeshData.cpp/hpp` — mesh data model, CSV/M420 parsing, color map
- `src/slic3r/GUI/3DBed.cpp/hpp` — overlay geometry + ImGui legend
- `src/slic3r/Utils/BedMeshSerial.cpp/hpp` — fetch/probe over USB serial
- `src/slic3r/GUI/Plater.cpp` (`fetch_bed_mesh`, `probe_bed_mesh`) — orchestrates source selection and progress UI
- `resources/shaders/140/bed_mesh_overlay.{vs,fs}` — per-vertex-color shaders
- `doc/Calibration_Guide.md` — user documentation

## Version

Current version defined in `version.inc`: **2.9.4** (base), **Filament-Edition-1.2.0** (fork)

```cmake
set(SLIC3R_APP_NAME "PrusaSlicer")
set(SLIC3R_VERSION "2.9.4")
set(SLIC3R_BUILD_ID "PrusaSlicer-${SLIC3R_VERSION}-Filament-Edition-1.2.0")
```
