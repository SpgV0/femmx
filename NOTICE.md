# Notice of Modifications

This repository, `FEMMX` (https://github.com/SpgV0/femmx), is a
derivative of Finite Element Method Magnetics (FEMM), originally distributed
by David C. Meeker at https://github.com/cenit/FEMM under the Aladdin Free
Public License v8 (see [license.txt](license.txt)). This repository
distributes the Program subject to the same license, at no charge, with
complete corresponding source code.

Per License section 2(c)(i), this file records repository-level
modifications and their dates. Any source file that is actually edited must
additionally carry a per-file notice (author, contact, date, purpose of the
change) at the point of modification.

## Change Log

See [CHANGELOG.md](CHANGELOG.md) for full technical detail on each change
(dated entries, most recent first). This section is the condensed,
license-required record of modifications.

- 2026-07-06: Repository cloned from https://github.com/cenit/FEMM
  (commit 7d9e8ed) and re-hosted at https://github.com/SpgV0/femmx.
  No source files were altered as part of this change.
- 2026-07-07: Added `femm/femmeLua.cpp` and `femm/FemmeDoc.h` changes
  (new `mi_setredraw` Lua command), `femm/FemmeView.cpp` changes
  (`DrawPSLG()` and Copy/Move handlers now honor redraw suppression),
  `femm/MOVECOPY.CPP` changes (incremental `EnforcePSLG()` overload fixing
  an O(n^2) Copy cost), and `femm/FemmeView.h`/`femm/FemmeView.cpp`/
  `femm/femm.rc`/`femm/resource.h` changes (experimental Dark Theme toggle).
  See per-file modification notices for author/contact/date/purpose.
- 2026-07-08: Added an optional CUDA-accelerated linear solve for fkn.exe's
  magnetostatic/DC solver: `fkn/spars_cuda.cu`/`fkn/spars_cuda.h` (new),
  `fkn/spars.h`/`fkn/spars.cpp` (`GPUAccel` member, `PCGSolveGPU()`),
  `fkn/femmedoccore.h`/`fkn/femmedoccore.cpp`/`fkn/main.cpp` (field
  plumbing), `femm/FemmeDoc.h`/`femm/FemmeDoc.cpp` (`.fem` file field),
  `femm/femmeLua.cpp` (`mi_setgpuaccel` Lua command), and
  `femm/probdlg.h`/`femm/probdlg.cpp`/`femm/femm.rc`/`femm/resource.h`
  (Problem Definition dialog checkbox). Off by default; requires the CUDA
  Toolkit and `-DENABLE_CUDA_SOLVER=ON` to build, and falls back to the CPU
  solver at run time if no usable GPU is found. See per-file modification
  notices for author/contact/date/purpose.
- 2026-07-09: Added a CPU/GPU load monitor window shown alongside the
  solver progress dialog while `fkn.exe` is solving: `fkn/LoadMonitorDlg.h`/
  `fkn/LoadMonitorDlg.cpp` (new), `fkn/fkn.cpp`/`fkn/fkn.rc`/
  `fkn/resource.h` (wiring). Builds unconditionally (no CUDA Toolkit
  required); GPU utilization is sampled via NVML, loaded dynamically at run
  time with a graceful fallback to CPU-only display if unavailable. See
  per-file modification notices for author/contact/date/purpose.
- 2026-07-09: Rebranded the fork from femm_plus to FEMMX: `femm/CMakeLists.txt`
  (executable renamed femm.exe -> femmx.exe), `femm/femm.rc` (window title,
  VERSIONINFO strings), `femm/femm.odl` (comment only), `script.nsi`
  (installer), `.github/workflows/ccpp.yml` (CI), `scripts/register_femm_com.ps1`,
  `fkn/fkn.cpp`/`fkn/CMakeLists.txt`/`fkn/spars.cpp`/`fkn/cspars.cpp` (comments/
  messages), `mathfemm/mathfemm.m`, `mathfemm/usage.nb`, and
  `octavefemm/mfiles/openfemm.m` (executable path references). The
  femm.ActiveFEMM COM ProgID and the solver executables (fkn.exe/csolv.exe/
  hsolv.exe/belasolv.exe) are unchanged. See per-file modification notices
  for author/contact/date/purpose.
- 2026-07-09: `fkn/LoadMonitorDlg.h`/`fkn/LoadMonitorDlg.cpp`/`fkn/fkn.cpp`
  changes: the CPU/GPU load monitor window now stays open after the solve
  finishes (interactive runs only) until the user closes it, via an
  `atexit()` gate. See per-file modification notices.
- 2026-07-09: Extended the optional CUDA-accelerated linear solve to
  fkn.exe's harmonic (AC/eddy-current) solver: `fkn/spars_cuda.h`/
  `fkn/spars_cuda.cu` (`CudaPBCGSolve`), `fkn/spars.h`/`fkn/cspars.cpp`
  (`CBigComplexLinProb::GPUAccel` member, `PBCGSolveGPU()`), `fkn/main.cpp`
  (field plumbing). Reuses the existing `GPUAccel` opt-in (checkbox/
  `mi_setgpuaccel`/`.fem` field) added 2026-07-08 -- no new UI or file
  format changes. See per-file modification notices for author/contact/
  date/purpose.
- 2026-07-09: `manual/magnlua.tex`: documented `mi_setgpuaccel` and
  `mi_setredraw`, the two Lua commands added since the
  `femm-4.2-22Oct2023` pre-fork baseline, in the Lua Scripting chapter
  of the manual. See per-file modification notice.
- 2026-07-09: Added local dev build scripts: `build_plain.bat`,
  `build_cuda.bat`, `build_femmx.ps1` (new). Fixed `build.ps1` (folder
  cleanup never matched the `build_win_release64_notriangle` directory
  `-ForceTriangle32bit` creates, letting a cached `ENABLE_CUDA_SOLVER=ON`
  leak into a later CPU-only build) and `fkn/CMakeLists.txt` (`-ccbin`
  alone doesn't override the `INCLUDE` environment variable, so `nvcc`
  could still resolve a too-new MSVC's STL headers; added an explicit
  `-I` for `-ccbin`'s own toolset headers). See per-file modification
  notices for author/contact/date/purpose.
- 2026-07-09: Restructured the installer to match the original FEMM 4.2
  installer's `C:\femm42` layout and bundle the Mathematica/Octave/
  Scilab interfaces and CUDA runtime DLLs: `script.nsi`,
  `CMakeLists.txt`, `installer/CMakeLists.txt` (new, moved the NSIS
  install step here to fix a CMake install-ordering bug), and
  `scifemm/CMakeLists.txt` (now builds `scilink.dll`, a real shared
  library, instead of an unused static one). Added Octave wrappers
  `octavefemm/mfiles/mi_setredraw.m`, `mi_setgpuaccel.m`,
  `get_solve_stats.m` (new). See per-file modification notices.
- 2026-07-09: The installer now self-registers the `femm.ActiveFEMM` COM
  class on install/uninstall (`script.nsi`). `scripts/register_femm_com.ps1`
  now snapshots any prior registration before overwriting it; new
  `scripts/unregister_femm_com.ps1` (new) restores or removes it
  afterwards, wired into `.github/workflows/ccpp.yml` as a cleanup step.
  See per-file modification notices.
- 2026-07-09: Fixed the CPU/GPU load monitor not updating during an
  interactive (GUI, not Lua-scripted) solve: `femm/LoadMonitorDlg.h`/
  `femm/LoadMonitorDlg.cpp` (`MarkSolveStart` now optionally watches a
  process handle via the existing sample timer), `femm/FemmeView.cpp`,
  `femm/hdrawView.cpp`, `femm/cdrawView.cpp`, `femm/beladrawView.cpp`
  (call it unconditionally, not just on the Lua-scripted path). Reworded
  the GPU-solve failure dialog to name non-convergence as a distinct
  cause from "no GPU found": `fkn/spars.cpp`, `fkn/cspars.cpp`,
  `fkn/spars_cuda.cu`. See per-file modification notices.
- 2026-07-09: Added the magnetics editor's Dark Theme toggle to the
  results/postprocessor window and batched its density-plot GDI calls:
  `femm/FemmviewView.h`/`femm/FemmviewView.cpp` (`ApplyTheme`, and
  `PlotFluxDensity`/`OnDraw` now accumulate each color band's
  sub-triangles for one `PolyPolygon()` call instead of one `Polygon()`
  call per sub-triangle), `femm/femm.rc` (View menu item). See per-file
  modification notices.
- 2026-07-09: `README.md` and `manual/manual.tex.in`: documented all of
  the above, plus the preexisting Dark Theme and CPU/GPU Load Monitor
  View-menu toggles, which had no manual coverage until now. See
  per-file modification notice on `manual/manual.tex.in`.
- 2026-07-10: Split `README.md`: its prior changelog content moved
  verbatim into new `CHANGELOG.md`, and `README.md` rewritten to instead
  describe the software and document build/install requirements and
  steps (Visual Studio/MFC, CMake, optional NSIS/CUDA/LaTeX, the
  `build_plain.bat`/`build_cuda.bat` wrappers, and the installer layout).
