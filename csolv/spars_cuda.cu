// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
// new file. Optional CUDA-accelerated linear solve for the current-flow
// solver's complex-symmetric linear system (CBigComplexLinProb::
// PBCGSolveMod, cspars.cpp) -- current flow has no real-valued
// formulation, unlike hsolv/belasolv, so this is the only CUDA kernel
// this target needs. Adapted from fkn/spars_cuda.cu's CudaPBCGSolve
// (same Jacobi-preconditioned, complex-symmetric BiCG shape, same
// GPU-resident matvec/dot/vector-update kernels), but with csolv's own
// convergence criterion: sqrt(|Dot(Z,R)| / res_o) vs. Precision*0.01 --
// see spars_cuda.h's header comment for why this deliberately does NOT
// match fkn's nrm(R)/normb criterion. A pleasant side effect: because
// csolv's own criterion reuses the SAME complex dot product the BiCG
// recurrence already computes each iteration (unlike fkn's, which needs
// a separate true-norm reduction purely for convergence), this kernel
// only needs one cublasZdotu call per iteration, not two.
#include "spars_cuda.h"

#include <cuda_runtime.h>
#include <cusparse.h>
#include <cublas_v2.h>

#include <cmath>
#include <cstdio>

namespace {

#define CUDA_CHECK(call)                                                    \
  do {                                                                      \
    cudaError_t _err = (call);                                              \
    if (_err != cudaSuccess) {                                              \
      fprintf(stderr, "CudaPBCGSolveMod: CUDA error %s at %s:%d\n",         \
          cudaGetErrorString(_err), __FILE__, __LINE__);                    \
      return 0;                                                             \
    }                                                                       \
  } while (0)

#define CUSPARSE_CHECK(call)                                                \
  do {                                                                      \
    cusparseStatus_t _st = (call);                                          \
    if (_st != CUSPARSE_STATUS_SUCCESS) {                                   \
      fprintf(stderr, "CudaPBCGSolveMod: cuSPARSE error %d at %s:%d\n",     \
          (int)_st, __FILE__, __LINE__);                                    \
      return 0;                                                             \
    }                                                                       \
  } while (0)

#define CUBLAS_CHECK(call)                                                  \
  do {                                                                      \
    cublasStatus_t _st = (call);                                            \
    if (_st != CUBLAS_STATUS_SUCCESS) {                                     \
      fprintf(stderr, "CudaPBCGSolveMod: cuBLAS error %d at %s:%d\n",       \
          (int)_st, __FILE__, __LINE__);                                    \
      return 0;                                                             \
    }                                                                       \
  } while (0)

__global__ void InvertComplexKernel(const cuDoubleComplex* diag, cuDoubleComplex* invDiag, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n)
    invDiag[i] = cuCdiv(make_cuDoubleComplex(1.0, 0.0), diag[i]);
}

// Z[i] = X[i] * invDiag[i]  (Jacobi preconditioner application)
__global__ void JacobiComplexKernel(const cuDoubleComplex* X, const cuDoubleComplex* invDiag, cuDoubleComplex* Z, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n)
    Z[i] = cuCmul(X[i], invDiag[i]);
}

// R[i] = b[i] - Av[i]
__global__ void ResidualComplexKernel(const cuDoubleComplex* b, const cuDoubleComplex* Av, cuDoubleComplex* R, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n)
    R[i] = cuCsub(b[i], Av[i]);
}

// Y[i] += alpha * X[i]  (alpha a complex scalar)
__global__ void AxpyComplexKernel(cuDoubleComplex alpha, const cuDoubleComplex* X, cuDoubleComplex* Y, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n)
    Y[i] = cuCadd(Y[i], cuCmul(alpha, X[i]));
}

// P[i] = Z[i] + rho * P[i]  (rho a complex scalar)
__global__ void ScaleAddComplexKernel(const cuDoubleComplex* Z, cuDoubleComplex rho, cuDoubleComplex* P, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n)
    P[i] = cuCadd(Z[i], cuCmul(rho, P[i]));
}

constexpr int kThreads = 256;
inline int Blocks(int n) { return (n + kThreads - 1) / kThreads; }

}  // namespace

extern "C" int CudaPBCGSolveMod(
    int n,
    const int* rowPtr, const int* colInd, const cuDoubleComplex* values, int nnz,
    const cuDoubleComplex* diag,
    const cuDoubleComplex* b,
    cuDoubleComplex* V,
    double res_o,
    double tolerance,
    int maxiter,
    int* out_iters) {
  if (n <= 0 || nnz <= 0)
    return 0;

  cusparseHandle_t cusparse = nullptr;
  cublasHandle_t cublas = nullptr;
  if (cusparseCreate(&cusparse) != CUSPARSE_STATUS_SUCCESS) {
    fprintf(stderr, "CudaPBCGSolveMod: cusparseCreate failed -- no CUDA-capable GPU "
                     "found, or driver/toolkit mismatch (run nvidia-smi).\n");
    return 0;
  }
  if (cublasCreate(&cublas) != CUBLAS_STATUS_SUCCESS) {
    fprintf(stderr, "CudaPBCGSolveMod: cublasCreate failed -- no CUDA-capable GPU "
                     "found, or driver/toolkit mismatch (run nvidia-smi).\n");
    cusparseDestroy(cusparse);
    return 0;
  }

  int *d_rowPtr = nullptr, *d_colInd = nullptr;
  cuDoubleComplex *d_values = nullptr, *d_diag = nullptr, *d_invDiag = nullptr;
  cuDoubleComplex *d_b = nullptr, *d_V = nullptr, *d_R = nullptr, *d_P = nullptr,
                  *d_U = nullptr, *d_Z = nullptr;
  void* dBuffer = nullptr;
  cusparseSpMatDescr_t matA = nullptr;
  cusparseDnVecDescr_t vecX = nullptr, vecY = nullptr;

  auto cleanup = [&]() {
    if (dBuffer) cudaFree(dBuffer);
    if (matA) cusparseDestroySpMat(matA);
    if (vecX) cusparseDestroyDnVec(vecX);
    if (vecY) cusparseDestroyDnVec(vecY);
    cudaFree(d_rowPtr);
    cudaFree(d_colInd);
    cudaFree(d_values);
    cudaFree(d_diag);
    cudaFree(d_invDiag);
    cudaFree(d_b);
    cudaFree(d_V);
    cudaFree(d_R);
    cudaFree(d_P);
    cudaFree(d_U);
    cudaFree(d_Z);
    cublasDestroy(cublas);
    cusparseDestroy(cusparse);
  };

  CUDA_CHECK(cudaMalloc(&d_rowPtr, (size_t)(n + 1) * sizeof(int)));
  CUDA_CHECK(cudaMalloc(&d_colInd, (size_t)nnz * sizeof(int)));
  CUDA_CHECK(cudaMalloc(&d_values, (size_t)nnz * sizeof(cuDoubleComplex)));
  CUDA_CHECK(cudaMalloc(&d_diag, (size_t)n * sizeof(cuDoubleComplex)));
  CUDA_CHECK(cudaMalloc(&d_invDiag, (size_t)n * sizeof(cuDoubleComplex)));
  CUDA_CHECK(cudaMalloc(&d_b, (size_t)n * sizeof(cuDoubleComplex)));
  CUDA_CHECK(cudaMalloc(&d_V, (size_t)n * sizeof(cuDoubleComplex)));
  CUDA_CHECK(cudaMalloc(&d_R, (size_t)n * sizeof(cuDoubleComplex)));
  CUDA_CHECK(cudaMalloc(&d_P, (size_t)n * sizeof(cuDoubleComplex)));
  CUDA_CHECK(cudaMalloc(&d_U, (size_t)n * sizeof(cuDoubleComplex)));
  CUDA_CHECK(cudaMalloc(&d_Z, (size_t)n * sizeof(cuDoubleComplex)));

  CUDA_CHECK(cudaMemcpy(d_rowPtr, rowPtr, (size_t)(n + 1) * sizeof(int), cudaMemcpyHostToDevice));
  CUDA_CHECK(cudaMemcpy(d_colInd, colInd, (size_t)nnz * sizeof(int), cudaMemcpyHostToDevice));
  CUDA_CHECK(cudaMemcpy(d_values, values, (size_t)nnz * sizeof(cuDoubleComplex), cudaMemcpyHostToDevice));
  CUDA_CHECK(cudaMemcpy(d_diag, diag, (size_t)n * sizeof(cuDoubleComplex), cudaMemcpyHostToDevice));
  CUDA_CHECK(cudaMemcpy(d_b, b, (size_t)n * sizeof(cuDoubleComplex), cudaMemcpyHostToDevice));
  // V always arrives holding a caller-computed initial guess (see the
  // header comment on CudaPBCGSolveMod) -- never zeroed here.
  CUDA_CHECK(cudaMemcpy(d_V, V, (size_t)n * sizeof(cuDoubleComplex), cudaMemcpyHostToDevice));

  InvertComplexKernel<<<Blocks(n), kThreads>>>(d_diag, d_invDiag, n);
  CUDA_CHECK(cudaGetLastError());

  CUSPARSE_CHECK(cusparseCreateCsr(&matA, n, n, nnz, d_rowPtr, d_colInd, d_values,
      CUSPARSE_INDEX_32I, CUSPARSE_INDEX_32I, CUSPARSE_INDEX_BASE_ZERO, CUDA_C_64F));
  CUSPARSE_CHECK(cusparseCreateDnVec(&vecX, n, d_P, CUDA_C_64F));
  CUSPARSE_CHECK(cusparseCreateDnVec(&vecY, n, d_U, CUDA_C_64F));

  cuDoubleComplex alpha = make_cuDoubleComplex(1.0, 0.0);
  cuDoubleComplex beta = make_cuDoubleComplex(0.0, 0.0);
  size_t bufferSize = 0;
  CUSPARSE_CHECK(cusparseSpMV_bufferSize(cusparse, CUSPARSE_OPERATION_NON_TRANSPOSE,
      &alpha, matA, vecX, &beta, vecY, CUDA_C_64F, CUSPARSE_SPMV_ALG_DEFAULT, &bufferSize));
  CUDA_CHECK(cudaMalloc(&dBuffer, bufferSize));

  auto matvec = [&](cuDoubleComplex* x, cuDoubleComplex* y) -> int {
    CUSPARSE_CHECK(cusparseDnVecSetValues(vecX, x));
    CUSPARSE_CHECK(cusparseDnVecSetValues(vecY, y));
    CUSPARSE_CHECK(cusparseSpMV(cusparse, CUSPARSE_OPERATION_NON_TRANSPOSE, &alpha, matA,
        vecX, &beta, vecY, CUDA_C_64F, CUSPARSE_SPMV_ALG_DEFAULT, dBuffer));
    return 1;
  };
  // Complex-symmetric (unconjugated) dot product -- matches
  // CBigComplexLinProb::Dot, used both for the BiCG recurrence AND
  // (unlike fkn's version) csolv's own convergence check directly.
  auto dot = [&](cuDoubleComplex* x, cuDoubleComplex* y, cuDoubleComplex* result) -> int {
    CUBLAS_CHECK(cublasZdotu(cublas, n, x, 1, y, 1, result));
    return 1;
  };

  // ---- mirrors CBigComplexLinProb::PBCGSolveMod (cspars.cpp) step for
  // step, from "form residual" onward (PCGSQStart's warm-start and the
  // res_o computation already ran on the CPU before this was called) ----

  // form residual: R = b - A*V
  if (!matvec(d_V, d_U)) { cleanup(); return 0; }
  ResidualComplexKernel<<<Blocks(n), kThreads>>>(d_b, d_U, d_R, n);
  CUDA_CHECK(cudaGetLastError());

  // form initial search direction: P = Z = M^-1 R
  JacobiComplexKernel<<<Blocks(n), kThreads>>>(d_R, d_invDiag, d_Z, n);
  CUDA_CHECK(cudaGetLastError());
  CUDA_CHECK(cudaMemcpy(d_P, d_Z, (size_t)n * sizeof(cuDoubleComplex), cudaMemcpyDeviceToDevice));
  cuDoubleComplex res = make_cuDoubleComplex(0.0, 0.0);
  if (!dot(d_Z, d_R, &res)) { cleanup(); return 0; }

  int iters = 0;
  double er;
  do {
    // step i)
    if (!matvec(d_P, d_U)) { cleanup(); return 0; }
    cuDoubleComplex pAp = make_cuDoubleComplex(0.0, 0.0);
    if (!dot(d_P, d_U, &pAp)) { cleanup(); return 0; }
    cuDoubleComplex del = cuCdiv(res, pAp);

    // step ii) V += del*P ; step iii) R -= del*U
    AxpyComplexKernel<<<Blocks(n), kThreads>>>(del, d_P, d_V, n);
    CUDA_CHECK(cudaGetLastError());
    AxpyComplexKernel<<<Blocks(n), kThreads>>>(cuCmul(make_cuDoubleComplex(-1.0, 0.0), del), d_U, d_R, n);
    CUDA_CHECK(cudaGetLastError());

    // step iv)
    JacobiComplexKernel<<<Blocks(n), kThreads>>>(d_R, d_invDiag, d_Z, n);
    CUDA_CHECK(cudaGetLastError());
    cuDoubleComplex res_new = make_cuDoubleComplex(0.0, 0.0);
    if (!dot(d_Z, d_R, &res_new)) { cleanup(); return 0; }
    cuDoubleComplex rho = cuCdiv(res_new, res);
    res = res_new;

    // step v) P = Z + rho*P
    ScaleAddComplexKernel<<<Blocks(n), kThreads>>>(d_Z, rho, d_P, n);
    CUDA_CHECK(cudaGetLastError());

    // convergence check -- matches PBCGSolveMod's own
    // "er = sqrt(res.Abs() / res_o)", reusing the SAME res just computed
    // for the recurrence (not a separate norm reduction).
    double resAbs = sqrt(res.x * res.x + res.y * res.y);
    er = sqrt(resAbs / res_o);
    iters++;
  } while (er > tolerance && iters < maxiter);

  CUDA_CHECK(cudaMemcpy(V, d_V, (size_t)n * sizeof(cuDoubleComplex), cudaMemcpyDeviceToHost));
  if (out_iters) *out_iters = iters;

  cleanup();
  if (er > tolerance) {
    fprintf(stderr, "CudaPBCGSolveMod: did not converge after %d iterations "
                     "(reached relative error %.3e, requested %.3e). The GPU "
                     "ran fine -- the Jacobi preconditioner used for GPU "
                     "parallelism is just weaker than the CPU's SSOR on this "
                     "particular matrix.\n", iters, er, tolerance);
    return 0;
  }
  return 1;
}
