# Changelog

All notable changes made in this fork (`femm_mods`,
https://github.com/spgryparis/femm_mods) relative to upstream FEMM
(https://github.com/cenit/FEMM) are documented here. See
[NOTICE.md](NOTICE.md) for the license-required (AFPL section 2(c)(i))
modification record, and per-file comments for the modification notices
required on each edited source file.

## [Unreleased] - branch `new_features`

### Added

- **`demo_models/` folder** with scripted, end-to-end examples that build
  and solve FEMM models entirely from Python via the `pyfemm` COM
  interface:
  - `straight_wire_field.py` — builds a current-carrying-wire
    magnetostatics problem, solves it, and validates the result against
    the closed-form (Ampere's law) solution.
  - `copy_redraw_benchmark.py` — benchmarks the new `mi_setredraw` command
    (see below) against a densely-drawn model, writing results to
    `demo_models/results/copy_benchmark.txt`.
  - `demo_models/README.md` documents prerequisites and usage for both.

- **`mi_setredraw(flag)` Lua/scripting command**
  (`femm/femmeLua.cpp`, `femm/FemmeDoc.h`) — lets a script suspend the
  magnetics editor's canvas redraw around a batch of edit operations
  (e.g. repeated `mi_copytranslate`/`mi_copyrotate` calls) and force a
  single refresh when re-enabled, instead of paying for a full-canvas
  redraw after every individual call.

- **`NOTICE.md`** — repository-level modification record required by the
  Aladdin Free Public License, section 2(c)(i).

### Fixed

- **`femm/FemmeView.cpp`**: `DrawPSLG()` (the eager, immediate-mode
  redraw used after most single-object edits) did not check
  `CFemmeDoc::NoDraw`, unlike `OnDraw()` (the `WM_PAINT` handler) which
  already did. This made the existing redraw-suppression mechanism
  (previously only used internally by DXF import) unreliable. `DrawPSLG()`
  now honors `NoDraw` as well.
- **`femm/FemmeView.cpp`**: `OnCopyObjects()`/`OnMoveObjects()` (the
  Edit > Copy / Edit > Move dialogs) now suspend redraw for the duration
  of the batch edit and refresh once at the end, using the same
  `NoDraw` mechanism exposed to scripts via `mi_setredraw`.
  Measured effect: 2.61x faster over 30 repeated copy actions against a
  1,600-block-label model (see `demo_models/results/copy_benchmark.txt`).

### Known issue (not fixed)

- `CFemmeDoc::EnforcePSLG()` (`femm/MOVECOPY.CPP`), called once at the end
  of every Copy/Move operation, rebuilds the entire node/segment/arc/block
  list from scratch by re-inserting every item through intersection-
  checking `Add*` calls. This is O(n²) in the total feature count and is
  likely the dominant cost for Copy/Move on very large drawings. Left
  as a follow-up given the risk of changing core geometry-insertion
  semantics.

## Infrastructure

- 2026-07-06: Repository cloned from https://github.com/cenit/FEMM
  (commit `7d9e8ed`) and re-hosted at
  https://github.com/spgryparis/femm_mods.
- Toolchain: builds via `build.ps1` / CMake + MSVC (Visual Studio Build
  Tools, C++ ATL/MFC component) on Windows; no source changes required
  for this, just local toolchain setup (CMake, ATL/MFC workload).
