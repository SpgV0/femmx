---
name: gpu-speedup-investigation
description: "Findings on GPU-accelerating FEMM's solver, and the recommended lower-risk CPU alternative — deferred, not yet implemented"
metadata: 
  node_type: memory
  type: project
  originSessionId: 0645e6ab-f4a7-4004-a39e-44c28675f293
---

Investigated (2026-07-07) whether FEMM's solvers (fkn=magnetics, csolv=
electrostatics, hsolv=heat flow, belasolv=current flow) could be
GPU-accelerated. Deferred — user wants to pick this up later, nothing
implemented or committed yet.

## Findings

- Machine has a real GPU (RTX 4060 Laptop, CUDA 12.7 driver) but **no CUDA
  Toolkit installed** (no `nvcc`, no dev libraries) — can't build GPU code
  as-is.
- `fkn/spars.cpp` `CBigLinProb::PCGSolve()` (~line 208, ~80 lines) is the
  real-valued ICCG/PCG linear solver; `fkn/cspars.cpp` has the complex
  (AC) variants. Small, clean, conceptually GPU-friendly (matvec + dot +
  AXPY loop).
- **Blocker**: the sparse matrix is stored as a hand-rolled **linked list
  per row** (`CEntry* next`), not CSR — GPU sparse libraries (cuSPARSE)
  need CSR. Porting means rewriting `Put`/`Get`/`AddTo`/`MultA`/`MultPC`
  and the assembly scatter.
- The SSOR preconditioner's forward/back-substitution is inherently
  sequential per row — needs reformulation (level-scheduling or switch to
  Jacobi/ILU0) to parallelize at all.
- No existing BLAS/OpenMP/threading anywhere in the solver code currently.
- Matrix assembly loop (`fkn/prob1big.cpp`, ~320 lines, one 3×3 element
  contribution per mesh triangle, `prob2big/3big/4big.cpp` are analogous
  variants) is embarrassingly parallel per element, but the scatter into
  shared matrix rows has write races, and Lua callbacks for
  functional magnetization direction aren't thread-safe as written.
- Problem sizes (given linked-list storage + O(n) search in Get/Put) look
  architected for thousands–low hundreds-of-thousands of elements, not
  millions.

## Recommendation given to user

Full GPU port = multi-hour+ rewrite of the sparse matrix core
(linked-list → CSR) plus preconditioner reformulation — a numerically
sensitive change to the actual physics solver where a subtle bug produces
*silently wrong field results*, not a crash. Not something to do
speculatively without much more validation than one session allows.

Lower-risk, actually-testable-this-session alternative: add OpenMP to the
assembly loop (and possibly the matvec) in fkn — real multi-core speedup,
no new toolchain, verifiable against [[femm_mods]]'s existing
`test_models/straight_wire_field.py` regression check (known analytical
answer).

**How to apply:** When the user returns to this, ask whether they want
(a) CPU OpenMP path (recommended, lower risk) or (b) still want the full
GPU port (would need `winget install Nvidia.CUDA` or similar first, then
the CSR rewrite). Don't re-run the investigation — this file has it.
