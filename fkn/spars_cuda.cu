// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-08:
// new file. Optional CUDA-accelerated linear solve, mirroring
// CBigLinProb::PCGSolve's algorithm (spars.cpp) step for step, but with a
// GPU-resident Jacobi preconditioner in place of SSOR. SSOR's forward/back
// substitution is a sequential row-by-row dependency chain and isn't
// GPU-parallelizable without a much larger reformulation (level scheduling
// or a switch to ILU0); Jacobi (Y[i] = X[i] / diag[i]) is embarrassingly
// parallel and lets the whole PCG loop -- matvec, dot products, vector
// updates, and the preconditioner -- run on the GPU without any per-
// iteration host<->device transfer. See fkn/spars_cuda.h for the entry
// point and test/gpu_solver_test.py for the correctness/benchmark
// check that motivated this trade-off (Jacobi needs more iterations than
// SSOR, but each GPU iteration is fast enough that it still wins well
// above roughly 15,000-20,000 unknowns).
// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-09:
// added CudaPBCGSolve, the complex-symmetric counterpart for
// CBigComplexLinProb::PBCGSolve (AC/harmonic eddy-current problems, see
// fkn/cspars.cpp's PBCGSolveGPU). Same Jacobi-preconditioner trade-off as
// above, plus a complex-symmetric (not Hermitian) inner product for the CG
// recurrence and a separate true-residual-norm check to match PBCGSolve's
// actual convergence criterion -- see the comment above CudaPBCGSolve.
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
      fprintf(stderr, "CudaPCGSolve: CUDA error %s at %s:%d\n",             \
          cudaGetErrorString(_err), __FILE__, __LINE__);                    \
      return 0;                                                             \
    }                                                                       \
  } while (0)

#define CUSPARSE_CHECK(call)                                                \
  do {                                                                      \
    cusparseStatus_t _st = (call);                                          \
    if (_st != CUSPARSE_STATUS_SUCCESS) {                                   \
      fprintf(stderr, "CudaPCGSolve: cuSPARSE error %d at %s:%d\n",         \
          (int)_st, __FILE__, __LINE__);                                    \
      return 0;                                                             \
    }                                                                       \
  } while (0)

#define CUBLAS_CHECK(call)                                                  \
  do {                                                                      \
    cublasStatus_t _st = (call);                                            \
    if (_st != CUBLAS_STATUS_SUCCESS) {                                     \
      fprintf(stderr, "CudaPCGSolve: cuBLAS error %d at %s:%d\n",           \
          (int)_st, __FILE__, __LINE__);                                    \
      return 0;                                                             \
    }                                                                       \
  } while (0)

__global__ void InvertKernel(const double* diag, double* invDiag, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n)
    invDiag[i] = 1.0 / diag[i];
}

// Z[i] = X[i] * invDiag[i]  (Jacobi preconditioner application)
__global__ void JacobiKernel(const double* X, const double* invDiag, double* Z, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n)
    Z[i] = X[i] * invDiag[i];
}

// R[i] = b[i] - Av[i]
__global__ void ResidualKernel(const double* b, const double* Av, double* R, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n)
    R[i] = b[i] - Av[i];
}

// Y[i] += alpha * X[i]
__global__ void AxpyKernel(double alpha, const double* X, double* Y, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n)
    Y[i] += alpha * X[i];
}

// P[i] = Z[i] + rho * P[i]
__global__ void ScaleAddKernel(const double* Z, double rho, double* P, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n)
    P[i] = Z[i] + rho * P[i];
}

constexpr int kThreads = 256;
inline int Blocks(int n) { return (n + kThreads - 1) / kThreads; }

// ---- complex-symmetric counterparts, for CudaPBCGSolve ----

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

}  // namespace

extern "C" int CudaPCGSolve(
    int n,
    const int* rowPtr, const int* colInd, const double* values, int nnz,
    const double* diag,
    const double* b,
    double* V,
    int flag,
    double precision,
    int maxiter,
    int* out_iters) {
  if (n <= 0 || nnz <= 0)
    return 0;

  cusparseHandle_t cusparse = nullptr;
  cublasHandle_t cublas = nullptr;
  if (cusparseCreate(&cusparse) != CUSPARSE_STATUS_SUCCESS) {
    fprintf(stderr, "CudaPCGSolve: cusparseCreate failed -- no CUDA-capable GPU "
                     "found, or driver/toolkit mismatch (run nvidia-smi).\n");
    return 0;
  }
  if (cublasCreate(&cublas) != CUBLAS_STATUS_SUCCESS) {
    fprintf(stderr, "CudaPCGSolve: cublasCreate failed -- no CUDA-capable GPU "
                     "found, or driver/toolkit mismatch (run nvidia-smi).\n");
    cusparseDestroy(cusparse);
    return 0;
  }

  int *d_rowPtr = nullptr, *d_colInd = nullptr;
  double *d_values = nullptr, *d_diag = nullptr, *d_invDiag = nullptr;
  double *d_b = nullptr, *d_V = nullptr, *d_R = nullptr, *d_P = nullptr,
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
  CUDA_CHECK(cudaMalloc(&d_values, (size_t)nnz * sizeof(double)));
  CUDA_CHECK(cudaMalloc(&d_diag, (size_t)n * sizeof(double)));
  CUDA_CHECK(cudaMalloc(&d_invDiag, (size_t)n * sizeof(double)));
  CUDA_CHECK(cudaMalloc(&d_b, (size_t)n * sizeof(double)));
  CUDA_CHECK(cudaMalloc(&d_V, (size_t)n * sizeof(double)));
  CUDA_CHECK(cudaMalloc(&d_R, (size_t)n * sizeof(double)));
  CUDA_CHECK(cudaMalloc(&d_P, (size_t)n * sizeof(double)));
  CUDA_CHECK(cudaMalloc(&d_U, (size_t)n * sizeof(double)));
  CUDA_CHECK(cudaMalloc(&d_Z, (size_t)n * sizeof(double)));

  CUDA_CHECK(cudaMemcpy(d_rowPtr, rowPtr, (size_t)(n + 1) * sizeof(int), cudaMemcpyHostToDevice));
  CUDA_CHECK(cudaMemcpy(d_colInd, colInd, (size_t)nnz * sizeof(int), cudaMemcpyHostToDevice));
  CUDA_CHECK(cudaMemcpy(d_values, values, (size_t)nnz * sizeof(double), cudaMemcpyHostToDevice));
  CUDA_CHECK(cudaMemcpy(d_diag, diag, (size_t)n * sizeof(double), cudaMemcpyHostToDevice));
  CUDA_CHECK(cudaMemcpy(d_b, b, (size_t)n * sizeof(double), cudaMemcpyHostToDevice));

  if (flag) {
    CUDA_CHECK(cudaMemcpy(d_V, V, (size_t)n * sizeof(double), cudaMemcpyHostToDevice));
  } else {
    CUDA_CHECK(cudaMemset(d_V, 0, (size_t)n * sizeof(double)));
  }

  InvertKernel<<<Blocks(n), kThreads>>>(d_diag, d_invDiag, n);
  CUDA_CHECK(cudaGetLastError());

  CUSPARSE_CHECK(cusparseCreateCsr(&matA, n, n, nnz, d_rowPtr, d_colInd, d_values,
      CUSPARSE_INDEX_32I, CUSPARSE_INDEX_32I, CUSPARSE_INDEX_BASE_ZERO, CUDA_R_64F));
  CUSPARSE_CHECK(cusparseCreateDnVec(&vecX, n, d_P, CUDA_R_64F));
  CUSPARSE_CHECK(cusparseCreateDnVec(&vecY, n, d_U, CUDA_R_64F));

  double alpha = 1.0, beta = 0.0;
  size_t bufferSize = 0;
  CUSPARSE_CHECK(cusparseSpMV_bufferSize(cusparse, CUSPARSE_OPERATION_NON_TRANSPOSE,
      &alpha, matA, vecX, &beta, vecY, CUDA_R_64F, CUSPARSE_SPMV_ALG_DEFAULT, &bufferSize));
  CUDA_CHECK(cudaMalloc(&dBuffer, bufferSize));

  // matvec(x, y): y = A * x, reusing the vecX/vecY descriptors by rebinding
  // their device pointers (avoids recreating descriptors every iteration).
  auto matvec = [&](double* x, double* y) -> int {
    CUSPARSE_CHECK(cusparseDnVecSetValues(vecX, x));
    CUSPARSE_CHECK(cusparseDnVecSetValues(vecY, y));
    CUSPARSE_CHECK(cusparseSpMV(cusparse, CUSPARSE_OPERATION_NON_TRANSPOSE, &alpha, matA,
        vecX, &beta, vecY, CUDA_R_64F, CUSPARSE_SPMV_ALG_DEFAULT, dBuffer));
    return 1;
  };
  auto dot = [&](double* x, double* y, double* result) -> int {
    CUBLAS_CHECK(cublasDdot(cublas, n, x, 1, y, 1, result));
    return 1;
  };

  // ---- mirrors CBigLinProb::PCGSolve (spars.cpp) step for step ----

  // residual with V=0 (res_o)
  JacobiKernel<<<Blocks(n), kThreads>>>(d_b, d_invDiag, d_Z, n);
  CUDA_CHECK(cudaGetLastError());
  double res_o = 0;
  if (!dot(d_Z, d_b, &res_o)) { cleanup(); return 0; }
  if (res_o == 0) {
    CUDA_CHECK(cudaMemcpy(V, d_V, (size_t)n * sizeof(double), cudaMemcpyDeviceToHost));
    if (out_iters) *out_iters = 0;
    cleanup();
    return 1;
  }

  // form residual: R = b - A*V
  if (!matvec(d_V, d_U)) { cleanup(); return 0; }
  ResidualKernel<<<Blocks(n), kThreads>>>(d_b, d_U, d_R, n);
  CUDA_CHECK(cudaGetLastError());

  // form initial search direction: P = Z = M^-1 R
  JacobiKernel<<<Blocks(n), kThreads>>>(d_R, d_invDiag, d_Z, n);
  CUDA_CHECK(cudaGetLastError());
  CUDA_CHECK(cudaMemcpy(d_P, d_Z, (size_t)n * sizeof(double), cudaMemcpyDeviceToDevice));
  double res = 0;
  if (!dot(d_Z, d_R, &res)) { cleanup(); return 0; }

  int iters = 0;
  double er;
  do {
    // step i)
    if (!matvec(d_P, d_U)) { cleanup(); return 0; }
    double pAp = 0;
    if (!dot(d_P, d_U, &pAp)) { cleanup(); return 0; }
    double del = res / pAp;

    // step ii) V += del*P ; step iii) R -= del*U
    AxpyKernel<<<Blocks(n), kThreads>>>(del, d_P, d_V, n);
    CUDA_CHECK(cudaGetLastError());
    AxpyKernel<<<Blocks(n), kThreads>>>(-del, d_U, d_R, n);
    CUDA_CHECK(cudaGetLastError());

    // step iv)
    JacobiKernel<<<Blocks(n), kThreads>>>(d_R, d_invDiag, d_Z, n);
    CUDA_CHECK(cudaGetLastError());
    double res_new = 0;
    if (!dot(d_Z, d_R, &res_new)) { cleanup(); return 0; }
    double rho = res_new / res;
    res = res_new;

    // step v) P = Z + rho*P
    ScaleAddKernel<<<Blocks(n), kThreads>>>(d_Z, rho, d_P, n);
    CUDA_CHECK(cudaGetLastError());

    er = sqrt(res / res_o);
    iters++;
  } while (er > precision && iters < maxiter);

  CUDA_CHECK(cudaMemcpy(V, d_V, (size_t)n * sizeof(double), cudaMemcpyDeviceToHost));
  if (out_iters) *out_iters = iters;

  cleanup();
  if (er > precision) {
    fprintf(stderr, "CudaPCGSolve: did not converge after %d iterations "
                     "(reached relative error %.3e, requested %.3e). The GPU "
                     "ran fine -- the Jacobi preconditioner used for GPU "
                     "parallelism is just weaker than the CPU's SSOR on this "
                     "particular matrix.\n", iters, er, precision);
    return 0;
  }
  return 1;
}

// ---- complex-symmetric solve, for CBigComplexLinProb::PBCGSolve ----
//
// Same Jacobi-preconditioned CG shape as CudaPCGSolve above, but: (a) the
// matrix is complex-symmetric (A = A^T, not Hermitian A = A^H), so the CG
// recurrence's inner products use the *unconjugated* bilinear dot
// (cublasZdotu, matching CBigComplexLinProb::Dot) rather than the conjugated
// Hermitian one; and (b) the convergence check mirrors PBCGSolve's actual
// criterion -- the true residual 2-norm ||R|| / ||b|| (cublasDznrm2, which
// computes sqrt(sum|x_i|^2), i.e. what CBigComplexLinProb::nrm() computes
// via Re(ConjDot(x,x)) -- not the res/res_o ratio CudaPCGSolve uses for the
// real case, since PBCGSolve itself checks a different quantity than
// PCGSolve does. See fkn/cspars.cpp's PBCGSolve for the CPU algorithm this
// mirrors step for step.
extern "C" int CudaPBCGSolve(
    int n,
    const int* rowPtr, const int* colInd, const cuDoubleComplex* values, int nnz,
    const cuDoubleComplex* diag,
    const cuDoubleComplex* b,
    cuDoubleComplex* V,
    double precision,
    int maxiter,
    int* out_iters) {
  if (n <= 0 || nnz <= 0)
    return 0;

  cusparseHandle_t cusparse = nullptr;
  cublasHandle_t cublas = nullptr;
  if (cusparseCreate(&cusparse) != CUSPARSE_STATUS_SUCCESS) {
    fprintf(stderr, "CudaPBCGSolve: cusparseCreate failed -- no CUDA-capable GPU "
                     "found, or driver/toolkit mismatch (run nvidia-smi).\n");
    return 0;
  }
  if (cublasCreate(&cublas) != CUBLAS_STATUS_SUCCESS) {
    fprintf(stderr, "CudaPBCGSolve: cublasCreate failed -- no CUDA-capable GPU "
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
  // header comment on CudaPBCGSolve) -- never zeroed here.
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
  // CBigComplexLinProb::Dot, used for the CG recurrence itself.
  auto dot = [&](cuDoubleComplex* x, cuDoubleComplex* y, cuDoubleComplex* result) -> int {
    CUBLAS_CHECK(cublasZdotu(cublas, n, x, 1, y, 1, result));
    return 1;
  };
  // True Euclidean 2-norm -- matches CBigComplexLinProb::nrm(), used only
  // for the convergence check (not the CG recurrence).
  auto norm2 = [&](cuDoubleComplex* x, double* result) -> int {
    CUBLAS_CHECK(cublasDznrm2(cublas, n, x, 1, result));
    return 1;
  };

  // ---- mirrors CBigComplexLinProb::PBCGSolve (cspars.cpp) step for step ----

  double normb = 0;
  if (!norm2(d_b, &normb)) { cleanup(); return 0; }
  if (normb == 0) {
    CUDA_CHECK(cudaMemcpy(V, d_V, (size_t)n * sizeof(cuDoubleComplex), cudaMemcpyDeviceToHost));
    if (out_iters) *out_iters = 0;
    cleanup();
    return 1;
  }

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

    // convergence check -- true residual norm, matching PBCGSolve's own
    // "er = nrm(R) / normb" (not a res/res_o ratio; see comment above).
    double nrmR = 0;
    if (!norm2(d_R, &nrmR)) { cleanup(); return 0; }
    er = nrmR / normb;
    iters++;
  } while (er > precision && iters < maxiter);

  CUDA_CHECK(cudaMemcpy(V, d_V, (size_t)n * sizeof(cuDoubleComplex), cudaMemcpyDeviceToHost));
  if (out_iters) *out_iters = iters;

  cleanup();
  if (er > precision) {
    fprintf(stderr, "CudaPBCGSolve: did not converge after %d iterations "
                     "(reached relative error %.3e, requested %.3e). The GPU "
                     "ran fine -- the Jacobi preconditioner used for GPU "
                     "parallelism is just weaker than the CPU's SSOR on this "
                     "particular matrix.\n", iters, er, precision);
    return 0;
  }
  return 1;
}
