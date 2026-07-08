# Notice of Modifications

This repository, `femm_plus` (https://github.com/SpgV0/femm_plus), is a
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
  (commit 7d9e8ed) and re-hosted at https://github.com/spgryparis/femm_mods.
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
- 2026-07-09: Repository migrated from https://github.com/spgryparis/femm_mods
  to https://github.com/SpgV0/femm_plus (renamed accordingly). No source
  files were altered as part of this change.
