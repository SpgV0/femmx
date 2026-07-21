---
name: hsolv-thermal-cuda-port
description: "CUDA-accelerated linear solve ported from fkn (magnetics) to hsolv (heat flow), plus hi_setgpuaccel/hi_setredraw Lua commands -- implemented and shipped on new_features"
metadata: 
  node_type: memory
  type: project
  originSessionId: 846a52dc-e5cc-4b0f-9a4f-7b5debeae297
  modified: 2026-07-21T11:17:02.546Z
---

Implemented and shipped 2026-07-21, commit `8e9093f` on `new_features` in
`femmx` (see [[push_branch_policy]]). Direct continuation of
[[gpu_speedup_investigation]]'s architecture, applied to the heat-flow
solver rather than magnetics.

## What shipped

- `hsolv/spars_cuda.h`/`.cu` (new): real-only port of `fkn/spars_cuda.h`/
  `.cu`'s `CudaPCGSolve` (no complex counterpart needed -- heat flow has
  no AC/harmonic formulation).
- `hsolv/SPARS.H`/`.CPP`: `CBigLinProb::GPUAccel` + `PCGSolveGPU()`,
  dispatched from `PCGSolve()` exactly like fkn's, adapted to hsolv's own
  `MsgBox`-based singularity-check style (no `fprintf(stderr,...)`) and
  `ChsolvDlg* TheView` (not `CFknDlg`).
  `hsolv/CMakeLists.txt`: full CUDA build wiring (nvcc custom command,
  `FEMM_CUDA_ENABLED` define, delay-loaded CUDA runtime DLLs, DLL
  install/copy) mirroring `fkn/CMakeLists.txt`, declared self-sufficiently
  (own `FEMM_CUDA_ROOT`/`FEMM_CUDA_CCBIN`/`FEMM_CUDA_ARCHS` cache vars,
  not assumed to inherit fkn's since subdirectory order isn't guaranteed).
  `ENABLE_CUDA_SOLVER` itself is declared once at the root CMakeLists.txt
  (added in an earlier session), visible to both fkn's and hsolv's
  subdirectories regardless of add_subdirectory() order (CMake cache vars
  are global once set).
- `hsolv/hsolvdoc.h`/`.cpp` (the solver's own `.feh` reader) + `MAIN.CPP`:
  `Doc.GPUAccel` parsed from `[GPUAccel]`, passed through to
  `L.GPUAccel` before `PCGSolve`. Default 0 (not gated on
  `FEMM_CUDA_ENABLED`) -- matches fkn/femmedoccore.cpp's convention: the
  solver-side default is always CPU-only; the GUI is what defaults it to
  1 on CUDA builds when creating a *new* problem.
- Classic GUI (`femm/HDRAWDOC.H`/`.CPP`): `GPUAccel` field, default
  `#ifdef FEMM_CUDA_ENABLED 1 #else 0` in `OnNewDocument` (mirrors
  `CFemmeDoc`'s identical pattern), `.feh` `[GPUAccel]` read/write.
- `femm/HDRAWLUA.CPP`: **both registration and implementation live in
  this one file** for heat flow (unlike magnetics, which splits across
  `femm/femmeLua.cpp`) -- `hi_setgpuaccel` (mirrors `mi_setgpuaccel`) and
  `hi_setredraw` (mirrors `mi_setredraw`; the underlying `NoDraw` field +
  `hdrawView.cpp` honoring it were *already* present for heat flow, just
  missing the Lua command to toggle it -- confirmed by grep before
  assuming it needed new plumbing).
- Documented both new commands in `manual/heatlua.tex` (Problem Commands
  / Editing Commands sections) and added `octavefemm/mfiles/
  hi_setgpuaccel.m` + `hi_setredraw.m` (one-function-per-file convention,
  each a thin `callfemm(...)` wrapper -- no CMakeLists/installer changes
  needed, `script.nsi` globs `octavefemm\mfiles\*.m`).
- Deliberately did **not** add a Problem Definition dialog checkbox for
  `hi_setgpuaccel` (magnetics has one, `IDC_GPUACCEL`) -- out of scope
  per explicit user direction this session ("don't bother with Qt for
  other problem types yet, just focus on the old GUI and solver
  accelerations"); the Lua command + `.feh` persistence is sufficient for
  scripted use and the regression test.
- `test/thermal_gpu_solver_test.py` (new): mirrors `gpu_solver_test.py`'s
  shape. Model is a radial heat-conduction annulus (fixed-temperature
  inner/outer boundaries via two pairs of arcs, `hi_addboundprop(name,
  0, Tset)` for Dirichlet), NOT a wire+ABC domain like magnetics --
  `hi_makeABC` exists for heat flow too but is unnecessary/inappropriate
  for a bounded conduction problem. **Gotcha caught before it could
  become a flaky test**: the disk inside the inner boundary (r <
  INNER_RADIUS) is a separate enclosed region that needs its own block
  label -- without one, `hi_analyze` fails outright with "Material
  properties have not been defined for all regions" (not a silent wrong
  answer). Fixed by adding a block label there with `hi_setblockprop("<No
  Mesh>", 0, 1, 0)` to mark it as an excluded hole, same sentinel
  mechanism as `.fem`'s `<No Mesh>` block type confirmed earlier during
  the `.femx`/`.ansx` port. Verified against the analytic steady-state
  solution T(r) = T1 + (T0-T1)*ln(r/r1)/ln(r2/r1): test measured
  T(20mm)=26.906, analytic=26.83 -- close enough to confirm the model
  and solve are physically correct, not just internally self-consistent.

## Verification

Built and tested on this machine's **plain (non-CUDA) build only** --
no CUDA-hardware speedup number exists yet for hsolv/heat-flow (unlike
[[gpu_speedup_investigation]]'s validated fkn numbers). CPU-vs-GPU
correctness assertion passed (0.0000% relative difference, since
GPUAccel=1 is a no-op on this non-CUDA build); the speedup assertion
correctly skipped (`_cuda_build_available()` heuristic). Before citing
any hsolv GPU speedup number, build with `-DENABLE_CUDA_SOLVER=ON` on a
CUDA-capable machine and rerun `test/thermal_gpu_solver_test.py` for
real -- don't assume it matches fkn's numbers, since
[[gpu_speedup_investigation]]'s own "Conflicting DC/real-solver
speedup measurement" section shows even fkn's real-valued (non-AC) GPU
path has had contradictory speedup results on the same problem class.
