// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
// new file. Optional CUDA-accelerated linear solve for
// CBigComplexLinProb::PBCGSolveMod (see spars_cuda.cu). Only compiled/
// linked when the csolv CMake target is built with -DENABLE_CUDA_SOLVER=ON;
// the declaration below is always visible so cspars.cpp can call it behind
// a FEMM_CUDA_ENABLED compile-time guard.
//
// Current flow's linear system is always complex-symmetric (a "conduction
// + displacement current" formulation, same shape as fkn's AC/harmonic
// solve) -- there is no real-valued counterpart to port, unlike
// hsolv/belasolv. NOT a copy of fkn/spars_cuda.h's CudaPBCGSolve: csolv's
// own CBigComplexLinProb::PBCGSolveMod (cspars.cpp) uses a DIFFERENT
// convergence criterion than fkn's CBigComplexLinProb::PBCGSolve --
// sqrt(|Dot(Z,R)| / res_o) compared against Precision*0.01 (the same
// "real"-style preconditioned-residual-ratio check the real-valued
// PCGSolve uses), not fkn's true-residual-2-norm ratio nrm(R)/normb. This
// kernel mirrors csolv's own criterion exactly rather than reusing fkn's,
// so a GPU-requested solve converges to the same stopping point the CPU
// path would have used.
#pragma once

#ifdef FEMM_CUDA_ENABLED

#include <cuComplex.h>

// Solves A*V = b for V using a GPU-resident Jacobi-preconditioned
// biconjugate-gradient method, where A is the complex-symmetric matrix
// (A = A^T, not Hermitian) described by the (rowPtr, colInd, values) CSR
// triple (full matrix, both triangles materialized -- NOT the
// upper-triangle-only storage CBigComplexLinProb uses internally; the
// caller is responsible for that conversion).
//
// V always arrives holding a caller-computed initial guess (matching
// PBCGSolveMod's own flow, which always has a valid V by the time it
// would call this -- either fresh from PCGSQStart, or carried over from
// a previous call) -- never zeroed here. res_o is the caller's own
// already-computed initial preconditioned-residual magnitude
// (abs(Dot(Z,V)) in cspars.cpp, computed on CPU since it's cheap and
// needed regardless of GPU/CPU path), passed straight through so this
// kernel's convergence check matches PBCGSolveMod's exactly. tolerance
// is the caller's fully-scaled stopping threshold (Precision*0.01).
//
// Returns 1 on success (V holds the solution), 0 on any failure (caller
// should fall back to the CPU solver -- this must never be the only path).
extern "C" int CudaPBCGSolveMod(
    int n,
    const int* rowPtr, const int* colInd, const cuDoubleComplex* values, int nnz,
    const cuDoubleComplex* diag,
    const cuDoubleComplex* b,
    cuDoubleComplex* V,
    double res_o,
    double tolerance,
    int maxiter,
    int* out_iters);

#endif // FEMM_CUDA_ENABLED
