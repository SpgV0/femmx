// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-08:
// new file. Optional CUDA-accelerated linear solve for CBigLinProb (see
// spars_cuda.cu). Only compiled/linked when the fkn CMake target is built
// with -DENABLE_CUDA_SOLVER=ON; the declaration below is always visible so
// spars.cpp can call it behind a FEMM_CUDA_ENABLED compile-time guard.
// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-09:
// added CudaPBCGSolve, the complex-symmetric counterpart used by
// CBigComplexLinProb::PBCGSolve (cspars.cpp) for AC/harmonic (eddy-current)
// problems -- see spars_cuda.cu for details. CComplex (complex.h) and
// cuDoubleComplex are both plain {double, double} pairs with no vtable, so
// the caller can cudaMemcpy a CComplex* host array directly into a
// cuDoubleComplex* device buffer without an intermediate conversion pass.
#pragma once

#ifdef FEMM_CUDA_ENABLED

#include <cuComplex.h>

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

// Complex-symmetric (NOT Hermitian) counterpart of CudaPCGSolve, for
// CBigComplexLinProb's AC/harmonic linear system A*V = b, where A is
// complex-symmetric (A = A^T, not A^H) -- the standard result of a
// time-harmonic FEM eddy-current formulation. Mirrors
// CBigComplexLinProb::PBCGSolve step for step, with the same SSOR->Jacobi
// preconditioner swap CudaPCGSolve makes for the real case. V is always
// treated as a pre-set initial guess (matching PBCGSolveMod's PBCGSolve(2)
// call, which never zeroes V itself -- the CPU-side PCGSQStart() warm-start
// already ran before this is called).
extern "C" int CudaPBCGSolve(
    int n,
    const int* rowPtr, const int* colInd, const cuDoubleComplex* values, int nnz,
    const cuDoubleComplex* diag,
    const cuDoubleComplex* b,
    cuDoubleComplex* V,
    double precision,
    int maxiter,
    int* out_iters);

#endif // FEMM_CUDA_ENABLED
