// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
// new file. Optional CUDA-accelerated linear solve for CBigLinProb (see
// spars_cuda.cu), ported from fkn/spars_cuda.h. Only compiled/linked when
// the hsolv CMake target is built with -DENABLE_CUDA_SOLVER=ON; the
// declaration below is always visible so spars.cpp can call it behind a
// FEMM_CUDA_ENABLED compile-time guard. Heat flow's linear system is always
// real-valued (no AC/harmonic formulation), so unlike fkn there is no
// complex-symmetric counterpart to port.
#pragma once

#ifdef FEMM_CUDA_ENABLED

// Solves A*V = b for V using a GPU-resident Jacobi-preconditioned conjugate
// gradient method, where A is the symmetric matrix described by the
// (rowPtr, colInd, values) CSR triple (full matrix, both triangles
// materialized -- NOT the upper-triangle-only storage CBigLinProb uses
// internally; the caller is responsible for that conversion).
//
// flag: 1 = V already holds an initial guess to refine; 0 = start from 0.
// Returns 1 on success (V holds the solution), 0 on any failure (caller
// should fall back to the CPU solver -- this must never be the only path).
extern "C" int CudaPCGSolve(
    int n,
    const int* rowPtr, const int* colInd, const double* values, int nnz,
    const double* diag,
    const double* b,
    double* V,
    int flag,
    double precision,
    int maxiter,
    int* out_iters);

#endif // FEMM_CUDA_ENABLED
