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

See [README.md](README.md) for full technical detail on each change (dated
entries, most recent first). This section is the condensed, license-required
record of modifications.

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
