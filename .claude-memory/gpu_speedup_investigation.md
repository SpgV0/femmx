---
name: gpu-speedup-investigation
description: "GPU-accelerated linear solve for FEMM's magnetostatic AND harmonic solvers, plus a CPU/GPU load monitor window — implemented, validated, and shipped on new_features in SpgV0/femmx"
metadata: 
  node_type: memory
  type: project
  originSessionId: 0645e6ab-f4a7-4004-a39e-44c28675f293
  modified: 2026-07-19T22:15:13.677Z
---

Investigated (2026-07-07), then implemented and shipped (2026-07-08).
Commit `f19a90e` on `new_features` in `femmx` (formerly `femm_mods`,
then `femm_plus`, now `SpgV0/femmx`; see [[push_branch_policy]]). Load monitor
window shipped 2026-07-09 in commit `4a3568d`, same branch. AC/harmonic
GPU solve (see below) shipped 2026-07-09 too, same branch.

## CPU profiling that motivated the AC/harmonic extension (2026-07-09)

Real phase-timing breakdown on the ~700K-node DC test model (GPU off,
temporary instrumentation, not committed): PCGSolve 51.95s (~85%),
LoadMesh 4.52s (~7%), Cuthill renumber 4.14s (~7%), assembly 0.39s
(<1%). Confirms the linear solve dominates everything else by an order
of magnitude, and that element-assembly parallelization (OpenMP,
embarrassingly parallel per-element except for the global-matrix
scatter step) is not worth pursuing for linear problems -- it stays
proportionally tiny even for nonlinear problems since both assembly and
solve repeat per Newton-Raphson iteration. This is what pointed at the
AC/harmonic solver (100% CPU-only until this session) as the next
target rather than assembly parallelization.

## AC/harmonic (eddy-current) GPU extension

`fkn/cspars.cpp`'s `PBCGSolveMod`/`PBCGSolve` (used for any
`Doc.Frequency != 0` problem -- motors, transformers, induction
heating, any eddy-current analysis) had zero GPU acceleration; only the
DC/magnetostatic path did. Structurally nearly identical to the DC case
(same SSOR-preconditioned iterative solve, same linked-list upper-
triangle matrix storage), just complex-symmetric (A = A^T, not
Hermitian) instead of real-symmetric.

- `fkn/spars_cuda.h`/`.cu`: added `CudaPBCGSolve`, mirroring
  `CudaPCGSolve`'s Jacobi-preconditioner-instead-of-SSOR trick, but with
  `cuDoubleComplex` throughout. Two complex dot products needed, not
  one: `cublasZdotu` (unconjugated, matches `CBigComplexLinProb::Dot`,
  used for the CG recurrence) and `cublasDznrm2` (matches `nrm()` =
  `sqrt(Re(ConjDot(x,x)))`, used only for the convergence check --
  `PBCGSolve`'s stopping criterion is `nrm(R)/nrm(b)`, a different
  quantity than the real case's `sqrt(res/res_o)` ratio, so this
  couldn't just reuse the real solver's convergence logic).
- `CComplex` (`fkn/complex.h`, `{double re, im;}`, no vtable) and
  `cuDoubleComplex` are binary-layout-compatible -- host code
  `reinterpret_cast`s contiguous `CComplex*` arrays (`b`, `V`) directly
  to `cuDoubleComplex*` rather than copying.
- `fkn/spars.h`/`fkn/cspars.cpp`: `CBigComplexLinProb::GPUAccel` +
  `PBCGSolveGPU()`, dispatched from `PBCGSolveMod` exactly where
  `PCGSolve` dispatches for the real case. Scoped to the non-Newton-
  Raphson path only (`bNewton` still goes straight to CPU
  `KludgeSolve`, an existing, rarer edge case) -- and `V` is always
  treated as a pre-set initial guess (never zeroed), since
  `PBCGSolveMod` always calls `PBCGSolve(2)`, never with V=0.
- Reuses the *existing* `GPUAccel` field end-to-end (checkbox in the
  Problem Definition dialog, `mi_setgpuaccel` Lua command, `.fem` file
  `[GPUAccel]` field) -- it was already problem-type-agnostic, just
  only consumed by the DC path before. One setting now covers both
  solvers; no new UI.
- Test: `test/ac_gpu_solver_test.py`, mirrors `gpu_solver_test.py`'s
  shape. Model needed a genuine conductor (not just the stranded
  excitation coil, which FEMM zeroes conductivity on -- see
  `bIsWound` in `fkn/prob1big.cpp`) to get real `jwsigma` terms and
  exercise complex arithmetic properly: added a passive solid copper
  disk near the coil, not part of any circuit. Gotcha: `mo_getb()`
  returns complex phasors for AC solutions, so the field magnitude is
  `sqrt(|bx|^2+|by|^2)` using complex `abs()`, not `math.hypot` (which
  rejects complex args).
- Validated on real hardware: **0.0000% relative difference**,
  **2.2-2.4x speedup** on a ~40K-node eddy-current problem.

## What shipped

- `fkn/spars_cuda.cu` (new): `CBigLinProb::PCGSolveGPU()` converts the
  linked-list upper-triangle matrix to full symmetric CSR and runs a
  GPU-resident Jacobi-preconditioned PCG loop (cuSPARSE matvec + custom
  kernels), mirroring `PCGSolve`'s CPU algorithm step for step. SSOR
  (the CPU default) is not GPU-parallelizable as-is (sequential
  forward/back substitution per row) — Jacobi trades more iterations
  for full GPU parallelism instead.
- Opt-in only: `CBigLinProb::GPUAccel` flag, set per-problem via the new
  `mi_setgpuaccel(flag)` Lua command or the "Use GPU Acceleration"
  checkbox in the Problem Definition dialog (`IDC_GPUACCEL`). Persisted
  in the `.fem` file as `[GPUAccel]`, following the exact same pattern
  as the existing `ACSolver` setting (fkn.exe is a separate process
  femm.exe invokes per solve, so this can't just be an in-memory flag).
- Build: `fkn/CMakeLists.txt` gained `ENABLE_CUDA_SOLVER` (OFF by
  default — zero effect on the normal CI/build.ps1 path). When ON,
  compiles `spars_cuda.cu` via a direct `nvcc` custom-command (CMake's
  native CUDA/MSBuild integration doesn't have a VS-2026-compatible
  CUDA Toolkit release yet — see toolchain section below) and bundles
  the CUDA runtime DLLs next to `fkn.exe`.
- Safety: if GPU acceleration is requested but unavailable (no CUDA
  build, or the GPU solve fails at runtime), `PCGSolve` shows a MsgBox
  with concrete fix instructions and transparently falls back to CPU.
  Safe to do even during scripted `mi_analyze()` — fkn's `MsgBox` only
  self-suppresses with 3+ argv (the `bLinehook` hidden-Lua path), and
  the normal 2-arg invocation does show it, so this needed checking
  (see toolchain section for how this surfaced a real bug).
- Tests: `test/gpu_solver_test.py` (folder renamed from `unittests/` on
  2026-07-08, cleanup of a leftover half-done rename finished 2026-07-09)
  solves a ~70K-node problem with GPU off/on. The correctness check
  (results agree within 0.1%) always runs regardless of CUDA
  availability (regression guard on the GPUAccel plumbing itself); the
  speedup check only runs if CUDA DLLs are detected next to `fkn.exe`.
- `fkn/LoadMonitorDlg.h`/`.cpp` (new, 2026-07-09): a modeless dialog
  (`Create(IDD_LOADMONITOR, NULL)`, shown before the solve thread starts
  in `fkn.cpp`'s `InitInstance()`) that plots a rolling 60s strip chart
  of CPU utilization (`GetSystemTimes` delta) and, when available, GPU
  utilization (NVML, `LoadLibraryA`-loaded at run time — no link-time
  CUDA dependency, so this dialog builds and works even in non-CUDA
  builds). Has a "Save as PNG..." button (GDI+). Confirmed via window
  enumeration to render alongside the existing `fkern` solver progress
  dialog during a real solve.

## Validated results (this machine: CUDA 12.6, RTX 4060 Laptop)

- Default (no-CUDA) build: byte-for-byte unaffected, all existing tests
  still pass.
- CUDA-enabled build, ~70K-node problem: **0.0000% difference** in
  computed field vs CPU, **1.32-1.35x speedup**.
- CUDA-enabled build, ~700K-node problem (10x mesh density, ad hoc
  script, not a committed test): **0.0000% difference**, **3.49x
  speedup** (78.9s CPU vs 22.6s GPU) — speedup grows with problem size
  as expected (see crossover note below).
- Earlier standalone cupy/cuSPARSE feasibility benchmark (before writing
  any C++) found the CPU-SSOR vs GPU-Jacobi crossover around 15-20K
  unknowns, growing to 24.6x by 300K — confirms GPU accel is a real win
  above FEMM's typical problem size, hence opt-in rather than default.

## Toolchain gotchas (if extending this or debugging on another machine)

- **CUDA/MSVC version deadlock**: this machine's Visual Studio (2026,
  v18.x, cl.exe 19.51) is newer than any CUDA Toolkit's tested host
  compiler support. CUDA 13.3's installer *does* register VS-2026
  BuildCustomizations, but 13.3 needs a newer NVIDIA driver than was
  installed. CUDA 12.6 matches the driver but its `cudafe++` frontend
  crashes (access violation) on VS 2026 headers even with
  `-allow-unsupported-compiler`. Fix: installed VS2022 Build Tools
  (`Microsoft.VisualStudio.2022.BuildTools` via winget, VCTools
  workload only) purely to get an nvcc-compatible `cl.exe`, and pass it
  to nvcc via `-ccbin`. See `fkn/CMakeLists.txt`'s `FEMM_CUDA_CCBIN`
  cache var.
- **CRT mismatch**: nvcc's default host-compile flags don't match this
  project's Debug/`/MDd` build (LNK2038 `_ITERATOR_DEBUG_LEVEL`/
  `RuntimeLibrary` mismatch). Fixed with
  `-Xcompiler "$<$<CONFIG:Debug>:/MDd>$<$<CONFIG:Release>:/MD>"`.
- **Missing runtime DLLs**: `fkn.exe` linked against `cudart64_12.dll`
  etc. but nothing put them on PATH or next to the exe, so it failed to
  even start — silently, with the OS loader rejecting it before
  `main()` ran, so no console output. femm.exe's own `mi_loadsolution`
  then showed a "*.ans not found" dialog (initially misdiagnosed as a
  long-path issue, which is a completely reasonable enough first guess
  it's worth remembering as a red herring here). `cusparse64_12.dll`
  also transitively needs `nvJitLink_120_0.dll` — not a direct
  dependency of `fkn.exe`, only surfaces once cuSPARSE actually
  executes, so `dumpbin /dependents` on `fkn.exe` alone won't show it;
  had to check `cusparse64_12.dll`'s own dependencies too. Fixed by
  having `fkn/CMakeLists.txt` glob-copy the CUDA runtime DLLs next to
  `fkn.exe` as a post-build step when `ENABLE_CUDA_SOLVER=ON`.
- `cublasLt64_12.dll`/`cusparse64_12.dll` are individually 100s of MB
  (many baked-in kernel variants) — expect `bin/` to grow by roughly
  900MB when CUDA support is bundled. Fine since `bin/` is gitignored,
  but worth knowing before being surprised by disk usage.
- **Local dev builds hide the CUDA DLLs from `test/gpu_solver_test.py`'s
  own detection heuristic.** `build_plain.bat`/`build_cuda.bat`
  (`build_femmx.ps1`) move everything out of top-level `bin\` into
  `bin\plain\`/`bin\cuda\` after each build, but
  `_cuda_build_available()` in both `gpu_solver_test.py` and
  `ac_gpu_solver_test.py` only globs top-level `bin\` for
  `cudart64_*.dll` (matching CI's flat-`bin\` layout, which never uses
  the variant-folder scheme). Net effect: running the speedup assertion
  locally against a `bin\cuda\` build needs `cudart64_*.dll` (just that
  one small file is enough) temporarily copied into top-level `bin\`
  first, or the test silently skips instead of actually validating
  speedup — confirmed directly (2026-07-19/20 session): building CUDA
  concurrently with an unrelated plain-build pytest run left stray CUDA
  DLLs sitting in top-level `bin\` mid-compile, which made the *plain*
  (non-CUDA) `femmx.exe` under test look CUDA-enabled to the heuristic
  and produced a false test failure (asserted a speedup a non-CUDA build
  obviously can't deliver) — root-caused to the concurrent build, not a
  real regression. Lesson: never run `build_cuda.bat`/`build_plain.bat`
  concurrently with a pytest run that might touch `gpu_solver_test.py`/
  `ac_gpu_solver_test.py`, and rebuild fully (both variants) with a
  `git clean -fdx bin/` in between if a build gets interrupted, or the
  next build's move-to-variant step can silently mix in a killed
  in-progress build's leftover files (confirmed same session: a killed
  `build_cuda.bat` left ~900MB of CUDA DLLs sitting in top-level `bin\`,
  which the next `build_plain.bat`'s "plain" installer happily bundled
  in too, producing a 664MB "plain" installer instead of the normal
  ~32MB one).

## Conflicting DC/real-solver speedup measurement (2026-07-20) -- needs investigation

A from-scratch, fully isolated rerun of `test/gpu_solver_test.py` (no
concurrent build, freshly built + verified CUDA binary, COM registered
directly at the CUDA `femmx.exe`) measured the **DC/magnetostatic** GPU
solve as **3x *slower*** than CPU (14.5s CPU vs 43.6s GPU, speedup
0.33x) on the test's own ~tens-of-thousands-of-node wire+ABC problem --
correctness still perfect (0.0000% difference), just not faster. This
directly conflicts with the "Validated results" section above (1.32x on
~70K nodes, 3.49x on ~700K nodes, measured 2026-07-09). The **AC/
harmonic** solver, tested back-to-back in the same session on a
similarly-sized problem, *did* show a genuine 3.16x speedup, so this
isn't a broken GPU/driver/build -- something specific to the DC/real
`PCGSolveGPU` path (or this specific problem's conditioning/iteration
count) regressed, or was never as fast as claimed for this exact problem
shape. Not investigated further that session (out of scope for what was
being worked on) -- worth a fresh profiling pass before trusting the
older DC speedup numbers above at face value. Left as an honest failing
`test_gpu_is_faster_when_available` assertion in
`test/results/gpu_solver_test/gpu_solver.txt` rather than papered over.
