---
name: gpu-speedup-investigation
description: "GPU-accelerated linear solve for FEMM's magnetostatic solver — implemented, validated, and shipped on new_features"
metadata: 
  node_type: memory
  type: project
  originSessionId: 0645e6ab-f4a7-4004-a39e-44c28675f293
---

Investigated (2026-07-07), then implemented and shipped (2026-07-08).
Commit `f19a90e` on `new_features` in `femm_plus` (formerly `femm_mods`).

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
- Tests: `unittests/gpu_solver_test.py` solves a ~70K-node problem with
  GPU off/on. The correctness check (results agree within 0.1%) always
  runs regardless of CUDA availability (regression guard on the
  GPUAccel plumbing itself); the speedup check only runs if CUDA DLLs
  are detected next to `fkn.exe`.

## Validated results (this machine: CUDA 12.6, RTX 4060 Laptop)

- Default (no-CUDA) build: byte-for-byte unaffected, all existing tests
  still pass.
- CUDA-enabled build: **0.0000% difference** in computed field vs CPU,
  **1.32x speedup** on a 70K-node problem.
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
