---
name: hsolv-thermal-cuda-port
description: "CUDA-accelerated linear solve ported from fkn (magnetics) to ALL THREE other solvers -- hsolv (heat), belasolv (electrostatics), csolv (current flow) -- plus *_setgpuaccel/*_setredraw Lua commands for each. Shipped on new_features."
metadata: 
  node_type: memory
  type: project
  originSessionId: 846a52dc-e5cc-4b0f-9a4f-7b5debeae297
  modified: 2026-07-21T14:12:28.158Z
---

Implemented and shipped 2026-07-21 in three commits on `new_features` in
`femmx` (see [[push_branch_policy]]): `8e9093f` (hsolv/heat), `6e1f271`
(belasolv/electrostatics + csolv/current-flow), `72f4b5f` (CHANGELOG +
CI fix). Direct continuation of [[gpu_speedup_investigation]]'s
architecture (originally fkn/magnetics-only), now applied to **all four**
FEMM solvers. Session order: hsolv first (own request), then belasolv +
csolv together (user: "now make the same steps for electrostatics and
current flow" + "GPU acceleration related and add the redraw function" +
"Also add the load monitor option in all of them").

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

Initially built/tested on this machine's plain (non-CUDA) build only.
**Updated 2026-07-21, same day**: also built with
`-DENABLE_CUDA_SOLVER=ON` (`-DFEMM_CUDA_ROOT="C:\Program Files\NVIDIA GPU
Computing Toolkit\CUDA\v12.6"`) on this machine's real CUDA 12.6 + RTX
GPU -- all three of hsolv/belasolv/csolv's `spars_cuda.cu` files compiled
via nvcc and linked cleanly, matching what the `win-cuda` GitHub Actions
job does (see the CI section below). Re-registered COM at
`bin\cuda\femmx.exe` (via `scripts\register_femm_com.ps1
-FemmExePath bin\cuda\femmx.exe`) and reran all three tests for real
numbers -- correctness still exact (0.0000% diff) for all three.
**Speedup at this problem size (~35K nodes): hsolv 1.02x, belasolv
1.00x, csolv 0.98x -- essentially none.** This matches
[[gpu_speedup_investigation]]'s own "Conflicting DC/real-solver speedup
measurement" finding for fkn's real-valued solver at similar sizes --
not a new regression, a consistent pattern across every real-valued
(non-AC) GPU path in this codebase so far. Left as an honest, documented
result (`test/results/*/[...]_gpu_solver.txt`, committed) rather than
tuned to force a pass -- `currentflow_gpu_solver_test.py`'s
`test_gpu_is_faster_when_available` assertion actually FAILS at this
problem size on real hardware (0.98x), though CI never hits this since
its `win-cuda` job deselects that assertion entirely (no GPU on CI
runners). **Before assuming any of these three solvers benefit from GPU
acceleration at typical problem sizes, don't just trust this note --
rerun for real; a larger problem (see fkn's own 700K-node 3.49x result)
might show genuine speedup where this ~35K-node test doesn't.**
Restored COM back to `bin\plain\femmx.exe` afterward and deleted the
temporary top-level `bin\cudart64_12.dll` copy (see
[[gpu_speedup_investigation]]'s "hides the CUDA DLLs from
gpu_solver_test.py's own detection heuristic" gotcha for why that copy
was needed at all) -- don't leave either change lying around for a
future session to trip over.

## CI verification (2026-07-21, prompted by "is the manual build working in CI too" / "do you need to update anything for CI")

Found and fixed a real gap: `.github/workflows/ccpp.yml`'s `win-cuda`
job hardcoded its pytest file list (only `gpu_solver_test.py`
`ac_gpu_solver_test.py`) rather than running the whole `test/` directory
like the plain `win-vcpkg` job does -- so it was already compiling
hsolv/belasolv/csolv with CUDA (shared `ENABLE_CUDA_SOLVER` option) but
never actually running their new tests. Fixed by adding the three new
test files to that line (commit `72f4b5f`).

Also **directly verified, not just read the config for**, two other
CI-relevant things this session, both confirmed working:
- **The manual PDF build**: CI's `manual/build_manual.bat` uses plain
  `latex.exe` -> `dvips.exe` -> `ps2pdf.exe` (NOT `xelatex`, despite
  `manual.tex`'s own misleading `!TEX program = XeLaTeX` magic comment
  at the top -- that comment is contradicted by the document's own
  `\usepackage[dvips]{graphicx}`/`[dvips]{hyperref}` options, and is
  just a stale/wrong editor hint, not what any actual build path uses).
  Ran that exact pipeline locally (this machine's MiKTeX is installed
  but not on PATH -- same class of issue as
  [[build_and_com_registration_gotchas]]'s choco-latex finding; used
  `C:\Users\spgry\AppData\Local\Programs\MiKTeX\miktex\bin\x64\` directly)
  and it succeeded end-to-end, producing a clean 164-page PDF with the
  new `hi_`/`ei_`/`ci_` `setgpuaccel`/`setredraw` content rendering
  correctly.
- **The CUDA build itself** (see above) -- confirms `win-cuda`'s
  `-DENABLE_CUDA_SOLVER=ON` build will succeed for all four solvers, not
  just fkn.

Also checked and confirmed no other CI changes needed: `test/
requirements.txt` already covers everything the 3 new test files import
(just `pytest`+`pyfemm`, nothing new); all 5 test files referenced in
the `win-cuda` pytest line actually exist; no other hardcoded
file/solver lists exist elsewhere in `ccpp.yml`.

## belasolv (electrostatics) -- same shape as hsolv

Structurally identical port: `belasolv/spars.h`/`.cpp` already had the
exact same `CBigLinProb` (real-valued, SSOR CPU / Jacobi GPU PCG) as
hsolv's, byte-for-byte same `PCGSolve` including the `MsgBox("singular
flag tripped...")` style and `Lambda=1.5` placement -- confirmed by
direct comparison before writing anything, not assumed. `ei_setgpuaccel`/
`ei_setredraw` added to `femm/beladrawLua.cpp` (doc pointer is
`pBeladrawDoc`, view class `CbeladrawView`). File extension `.fee`.
`test/electrostatic_gpu_solver_test.py` reuses the exact same annulus
model as the thermal test (same Laplace equation, same analytic profile)
-- passed first try, T(20mm)=26.906 again (same geometry -> same answer).

## csolv (current flow) -- the one that's genuinely different

**csolv has NO real-valued solver at all** -- only `CBigComplexLinProb`
(current flow is inherently conduction + displacement current, always
complex-symmetric, same shape as fkn's AC/harmonic case). File extension
`.fec`.

Critical gotcha, caught by reading the CPU code before porting rather
than assuming fkn's AC kernel would just work: **csolv's
`PBCGSolveMod` uses a DIFFERENT convergence criterion than fkn's
`PBCGSolve`** -- csolv checks `sqrt(|Dot(Z,R)| / res_o) > Precision*0.01`
(the same "real-style" preconditioned-residual-ratio shape `PCGSolve`
uses, just complex), NOT fkn's `nrm(R)/normb` true-residual-2-norm
ratio. Blindly reusing fkn's `CudaPBCGSolve` would have converged to a
different stopping point than the CPU path. Wrote a distinct kernel,
`CudaPBCGSolveMod` (`csolv/spars_cuda.cu`/`.h`), matching csolv's own
criterion exactly -- a nice side effect: since csolv's own convergence
check reuses the SAME `Dot(Z,R)` value the recurrence step already
computes, this kernel needs only ONE `cublasZdotu` call per iteration
(fkn's needs two: one for the recurrence, one for a separate norm2).
`res_o` is computed cheaply on the CPU side (as csolv's own code already
does) and passed into the CUDA function as a parameter, rather than
replicated inside the kernel.

`ci_setgpuaccel`/`ci_setredraw` added to `femm/CDRAWLUA.CPP` (doc
pointer `pcdrawDoc`, view class `CcdrawView`).

**Test model needed a deliberately non-degenerate design**: a single
homogeneous material's complex admittivity (sigma + j*w*epsilon) factors
straight out of the governing equation for real Dirichlet BCs, collapsing
back to a purely REAL field regardless of frequency -- same lesson
[[gpu_speedup_investigation]] already learned for fkn's AC test needing a
"genuine conductor." Fixed with a TWO-material annulus (different
sigma AND epsilon ratios in each ring, no boundary condition on the
material interface arc -- natural FEM continuity handles it, same
technique as the wire/air interface in `gpu_solver_test.py`) at a
deliberately high test frequency (1 GHz -- unrealistic for real current-
flow physics, but needed to make w*epsilon comparable to sigma).
Result: genuinely complex, V = 48.99 - 29.28j, CPU and GPU matched
exactly. `test/currentflow_gpu_solver_test.py` compares by `abs()`
magnitude (same reasoning as `mo_getb()` in the AC magnetics test).

## Real pre-existing bug found and fixed along the way (unrelated to CUDA)

The CPU/GPU/RAM Load Monitor's "currently solving" label text was
SWAPPED between current flow and electrostatics: `femm/cdrawView.cpp`
(which invokes `csolv.exe`, current flow) said `"electrostatics: "`, and
`femm/beladrawView.cpp` (which invokes `belasolv.exe`, electrostatics)
said `"current flow: "` -- backwards. Found while checking whether "add
the load monitor to all of them" (a mid-turn user request) needed new
work; it turned out the Load Monitor is already a single, shared,
app-level dialog (`CMainFrame::m_LoadMonitor`, wired via
`MarkSolveStart`/`MarkSolveEnd` in every `*drawView.cpp`), so nothing
needed adding -- just this one swapped-label fix.

## CI gap found and fixed

`.github/workflows/ccpp.yml`'s `win-cuda` job hardcodes its pytest test
file list (`gpu_solver_test.py ac_gpu_solver_test.py ...`) rather than
running the whole `test/` directory the way the plain `win-vcpkg` job
does. Since `ENABLE_CUDA_SOLVER` is one shared root-level option, that
job was already compiling hsolv/belasolv/csolv with CUDA but never
actually running their tests. Added the three new test files to that
line -- easy to miss since the build itself doesn't fail from this, only
test coverage silently doesn't run. **Worth checking this same hardcoded
list again if a 5th solver/physics type ever gets CUDA support.**

## Manual-build note: MiKTeX is installed on this machine, just not on PATH

`build.ps1`'s LaTeX auto-detect (`Get-Command latex`) reports "LaTeX has
not been found!" on this machine, but MiKTeX IS actually installed at
`C:\Users\spgry\AppData\Local\Programs\MiKTeX\miktex\bin\x64\` (same
"installed but not on PATH" class of issue as
[[build_and_com_registration_gotchas]]'s choco-latex finding). To build
`manual.pdf` directly (bypassing `build.ps1`/CMake's `LATEX_COMPILER`
auto-detect entirely): run that directory's `xelatex.exe` twice against
`manual/manual.tex` (`-interaction=nonstopmode`, two passes for correct
cross-references) -- the `.tex` source's own `!TEX program = XeLaTeX`
directive confirms this is the intended engine, not the older
`latex`->`dvips`->`ps2pdf` pipeline `manual/CMakeLists.txt` uses.
Produced a real 168-page PDF both times; a `hyperref` "Wrong DVI mode
driver option `dvips`" package error appears in the log but is
NON-fatal (pre-existing document/engine mismatch, not something this
session's `.tex` edits caused) -- the PDF still completes with the
correct page count. `manual.pdf`/`manual.tex` are both gitignored
(`.gitignore` lines 33/38) so a locally-built copy can never be pushed --
deliver it directly to the user (e.g. via SendUserFile) when asked to
"upload the manual," don't try to commit it.
