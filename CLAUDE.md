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

Test data lives in `tests/data/`. Shared test data defined in `tests/prusaparts.cpp`.

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

1. **`DynamicPrintConfig`** (`Config.hpp`) — runtime key-value pairs, used by GUI, supports arbitrary merging
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
- Brace style: opening brace on same line for classes/structs/namespaces
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

GitHub Actions workflows in `.github/workflows/` (all delegated to `Prusa-Development/PrusaSlicer-Actions`):

| Workflow | Trigger | Purpose |
|----------|---------|---------|
| `build_osx.yml` | push + dispatch | macOS build |
| `build_windows.yml` | push + dispatch | Windows build (optional PDB + installer) |
| `build_flatpak.yml` | push + dispatch | Flatpak (Linux) build |
| `build_nogui.yml` | dispatch only | CLI-only build |
| `build_osx_asan.yml` | master + dispatch | macOS AddressSanitizer build |
| `build_flatpak_asan.yml` | master + dispatch | Flatpak ASan + debug symbols |
| `static_analysis.yml` | daily (0:00 UTC) + dispatch | Static analysis on `ms_dev` branch |
| `bundle_flatpak.yml` | dispatch | Flatpak bundling |

## Localization

gettext-based workflow with custom CMake targets:
- `gettext_make_pot` — extract strings
- `gettext_merge_community_po_with_pot` — merge community translations
- `gettext_concat_wx_po_with_po` — merge wxWidgets translations
- `gettext_po_to_mo` — compile .po to .mo
- Languages: cs, de, es, fr, it, ja, pl (and more in resources)

## Version

Current version defined in `version.inc`: **2.9.4**

```cmake
set(SLIC3R_APP_NAME "PrusaSlicer")
set(SLIC3R_VERSION "2.9.4")
```
