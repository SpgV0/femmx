#include <stdafx.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include "fkn.h"
#include "fknDlg.h"
#include "complex.h"
#include "spars.h"
#include "spars_cuda.h"

// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-08:
// PCGSolveGPU() (CSR conversion + dispatch to the optional CUDA solver)
// added below PCGSolve; PCGSolve now tries the GPU path first when
// GPUAccel is set, falling back to the existing CPU path unchanged on
// any failure or when built without CUDA support.

#define KLUDGE

CEntry::CEntry()
{
  next = NULL;
  x = 0;
  c = 0;
}

CBigLinProb::CBigLinProb()
{
  n = 0;
  GPUAccel = 0;
}

CBigLinProb::~CBigLinProb()
{
  if (n == 0)
    return;

  int i;
  CEntry *uo, *ui;

  free(b);
  free(P);
  free(R);
  free(V);
  free(U);
  free(Z);

  for (i = 0; i < n; i++) {
    ui = M[i];
    do {
      uo = ui;
      ui = uo->next;
      delete uo;
    } while (ui != NULL);
  }

  free(M);
}

int CBigLinProb::Create(int d, int bw)
{
  int i;

  bdw = bw;
  b = (double*)calloc(d, sizeof(double));
  V = (double*)calloc(d, sizeof(double));
  P = (double*)calloc(d, sizeof(double));
  R = (double*)calloc(d, sizeof(double));
  U = (double*)calloc(d, sizeof(double));
  Z = (double*)calloc(d, sizeof(double));

  M = (CEntry**)calloc(d, sizeof(CEntry*));
  n = d;

  for (i = 0; i < d; i++) {
    M[i] = new CEntry;
    M[i]->c = i;
  }

  return 1;
}

void CBigLinProb::Put(double v, int p, int q)
{
  CEntry *e, *l;
  int i;

  if (q < p) {
    i = p;
    p = q;
    q = i;
  }

  e = M[p];

  while ((e->c < q) && (e->next != NULL)) {
    l = e;
    e = e->next;
  }

  if (e->c == q) {
    e->x = v;
    return;
  }

  CEntry* m = new CEntry;

  if ((e->next == NULL) && (q > e->c)) {
    e->next = m;
    m->c = q;
    m->x = v;
  } else {
    l->next = m;
    m->next = e;
    m->c = q;
    m->x = v;
  }
  return;
}

double CBigLinProb::Get(int p, int q)
{
  CEntry* e;

  if (q < p) {
    int i;
    i = p;
    p = q;
    q = i;
  }

  e = M[p];

  while ((e->c < q) && (e->next != NULL))
    e = e->next;

  if (e->c == q)
    return e->x;

  return 0;
}

void CBigLinProb::AddTo(double v, int p, int q)
{
  Put(Get(p, q) + v, p, q);
}

void CBigLinProb::MultA(double* X, double* Y)
{
  int i;
  CEntry* e;

  for (i = 0; i < n; i++)
    Y[i] = 0;

  for (i = 0; i < n; i++) {
    Y[i] += M[i]->x * X[i];
    e = M[i]->next;
    while (e != NULL) {
      Y[i] += e->x * X[e->c];
      Y[e->c] += e->x * X[i];
      e = e->next;
    }
  }
}

double CBigLinProb::Dot(double* X, double* Y)
{
  int i;
  double z;

  for (i = 0, z = 0; i < n; i++)
    z += X[i] * Y[i];

  return z;
}

void CBigLinProb::MultPC(double* X, double* Y)
{
  // Jacobi preconditioner:
  //	int i;
  // for(i=0;i<n;i++) Y[i]=X[i]/M[i]->x;

  // SSOR preconditioner:
  int i;
  double c;
  CEntry* e;

  c = LAMBDA * (2. - LAMBDA);
  for (i = 0; i < n; i++)
    Y[i] = X[i] * c;

  // invert Lower Triangle;
  for (i = 0; i < n; i++) {
    Y[i] /= M[i]->x;
    e = M[i]->next;
    while (e != NULL) {
      Y[e->c] -= e->x * Y[i] * LAMBDA;
      e = e->next;
    }
  }

  for (i = 0; i < n; i++)
    Y[i] *= M[i]->x;

  // invert Upper Triangle
  for (i = n - 1; i >= 0; i--) {
    e = M[i]->next;
    while (e != NULL) {
      Y[i] -= e->x * Y[e->c] * LAMBDA;
      e = e->next;
    }
    Y[i] /= M[i]->x;
  }
}

int CBigLinProb::PCGSolve(int flag)
{
  int i;
  double res, res_o, res_new;
  double er, del, rho, pAp;

  // quick check for most obvious sign of singularity;
  for (i = 0; i < n; i++)
    if (M[i]->x == 0) {
      fprintf(stderr, "singular flag tripped.");
      return 0;
    }

  if (GPUAccel) {
#ifndef FEMM_CUDA_ENABLED
    MsgBox(
        "GPU acceleration was requested for this problem, but this copy of "
        "fkn.exe was built without CUDA support.\n\n"
        "To use GPU acceleration:\n"
        "1. Install the NVIDIA CUDA Toolkit (a version supported by your GPU "
        "driver -- run \"nvidia-smi\" to check the maximum CUDA version it "
        "supports).\n"
        "2. Rebuild femmx with CMake options -DENABLE_CUDA_SOLVER=ON "
        "-DFEMM_CUDA_ROOT=\"<path to CUDA Toolkit>\" (add "
        "-DFEMM_CUDA_CCBIN=\"<path to an nvcc-compatible MSVC toolset bin "
        "dir>\" too if your installed Visual Studio is newer than the CUDA "
        "Toolkit supports -- this is common; see fkn/CMakeLists.txt).\n"
        "3. See fkn/spars_cuda.cu and fkn/CMakeLists.txt for details.\n\n"
        "Continuing with the CPU solver for this run.");
#else
    if (TheView != NULL)
      TheView->SetDlgItemText(IDC_FRAME1, "Conjugate Gradient Solver (GPU)");
    if (PCGSolveGPU(flag))
      return 1;
    MsgBox(
        "GPU-accelerated solve failed or no usable GPU was found at run "
        "time (see the console/stderr output above for details).\n\n"
        "This usually means either:\n"
        "- No CUDA-capable NVIDIA GPU is present on this machine, or\n"
        "- The installed NVIDIA driver is older than this build's CUDA "
        "Toolkit requires -- run \"nvidia-smi\" to check the maximum CUDA "
        "version your driver supports, then either update the driver or "
        "rebuild fkn.exe with an older -DFEMM_CUDA_ROOT to match it.\n\n"
        "Continuing with the CPU solver for this run.");
#endif
    // GPU path unavailable, not built with CUDA support, or failed to
    // converge/errored out -- fall through to the CPU solve below rather
    // than failing the whole analysis.
    fprintf(stderr, "GPU-accelerated solve unavailable or failed; falling back to CPU.\n");
  }

  // initialize progress bar;
  TheView->SetDlgItemText(IDC_FRAME1, "Conjugate Gradient Solver");
  TheView->m_prog1.SetPos(0);
  int prg1 = 0;
  int prg2;

  // residual with V=0
  MultPC(b, Z);
  res_o = Dot(Z, b);
  if (res_o == 0)
    return 1;

  // if flag is false, initialize V with zeros;
  if (flag == 0)
    for (i = 0; i < n; i++)
      V[i] = 0;

  // form residual;
  MultA(V, R);
  for (i = 0; i < n; i++)
    R[i] = b[i] - R[i];

  // form initial search direction;
  MultPC(R, Z);
  for (i = 0; i < n; i++)
    P[i] = Z[i];
  res = Dot(Z, R);

  // do iteration;
  do {
    // step i)
    MultA(P, U);
    pAp = Dot(P, U);
    del = res / pAp;

    // step ii)
    for (i = 0; i < n; i++)
      V[i] += (del * P[i]);

    // step iii)
    for (i = 0; i < n; i++)
      R[i] -= (del * U[i]);

    // step iv)
    MultPC(R, Z);
    res_new = Dot(Z, R);
    rho = res_new / res;
    res = res_new;

    // step v)
    for (i = 0; i < n; i++)
      P[i] = Z[i] + (rho * P[i]);

    // have we converged yet?
    er = sqrt(res / res_o);
    prg2 = (int)(20. * log10(er) / (log10(Precision)));
    if (prg2 > prg1) {
      prg1 = prg2;
      prg2 = (prg1 * 5);
      if (prg2 > 100)
        prg2 = 100;
      TheView->m_prog1.SetPos(prg2);
      TheView->InvalidateRect(NULL, FALSE);
      TheView->UpdateWindow();
    }

  } while (er > Precision);

  return 1;
}

// Converts the upper-triangle-only linked-list matrix storage (M[]) into a
// full symmetric CSR matrix and dispatches to the CUDA solver. Returns
// false (never throws/errors out loud) if this build wasn't compiled with
// CUDA support, or if the GPU solve itself fails for any reason -- either
// way the caller (PCGSolve) falls back to the CPU path.
bool CBigLinProb::PCGSolveGPU(int flag)
{
#ifndef FEMM_CUDA_ENABLED
  (void)flag;
  return false;
#else
  int i;
  CEntry* e;

  // Pass 1: count entries per row of the FULL symmetric matrix. Each
  // stored entry (i, e->c) contributes to row i directly, and -- unless
  // it's the diagonal -- also mirrors into row e->c.
  std::vector<int> rowCount(n, 0);
  int nnz = 0;
  for (i = 0; i < n; i++) {
    for (e = M[i]; e != NULL; e = e->next) {
      rowCount[i]++;
      nnz++;
      if (e->c != i) {
        rowCount[e->c]++;
        nnz++;
      }
    }
  }

  std::vector<int> rowPtr(n + 1, 0);
  for (i = 0; i < n; i++)
    rowPtr[i + 1] = rowPtr[i] + rowCount[i];

  std::vector<int> colInd(nnz);
  std::vector<double> values(nnz);
  std::vector<double> diag(n);
  std::vector<int> cursor(rowPtr.begin(), rowPtr.end() - 1);

  for (i = 0; i < n; i++) {
    for (e = M[i]; e != NULL; e = e->next) {
      colInd[cursor[i]] = e->c;
      values[cursor[i]] = e->x;
      cursor[i]++;
      if (e->c != i) {
        colInd[cursor[e->c]] = i;
        values[cursor[e->c]] = e->x;
        cursor[e->c]++;
      }
    }
    diag[i] = M[i]->x; // M[i] is always the diagonal entry (see Create())
  }

  int iters = 0;
  int ok = CudaPCGSolve(n, rowPtr.data(), colInd.data(), values.data(), nnz,
      diag.data(), b, V, flag, Precision, /*maxiter=*/100000, &iters);

  return ok != 0;
#endif
}

void CBigLinProb::SetValue(int i, double x)
{
  int k, fst, lst;
  double z;

  if (bdw == 0) {
    fst = 0;
    lst = n;
  } else {
    fst = i - bdw;
    if (fst < 0)
      fst = 0;
    lst = i + bdw;
    if (lst > n)
      lst = n;
  }

  for (k = fst; k < lst; k++) {
    z = Get(k, i);
    if (z != 0) {
      b[k] = b[k] - (z * x);
      if (i != k)
        Put(0., k, i);
    }
  }
  b[i] = Get(i, i) * x;
}

void CBigLinProb::Wipe()
{
  int i;
  CEntry* e;

  for (i = 0; i < n; i++) {
    b[i] = 0.;
    e = M[i];
    do {
      e->x = 0;
      e = e->next;
    } while (e != NULL);
  }
}

void CBigLinProb::AntiPeriodicity(int i, int j)
{
  int k, fst, lst;
  double v1, v2, c;

#ifdef KLUDGE
  int tmpbdw = bdw;
  bdw = 0;
#endif

  if (j < i) {
    k = j;
    j = i;
    i = k;
  }

  if (bdw == 0) {
    fst = 0;
    lst = n;
  } else {
    fst = i - bdw;
    if (fst < 0)
      fst = 0;
    lst = j + bdw;
    if (lst > n)
      lst = n;
  }

  for (k = fst; k < lst; k++) {
    if ((k != i) && (k != j)) {
      v1 = Get(k, i);
      v2 = Get(k, j);
      if ((v1 != 0) || (v2 != 0)) {
        c = (v1 - v2) / 2.;
        Put(c, k, i);
        Put(-c, k, j);
      }
    }
    if ((k == i + bdw) && (k < j - bdw) && (bdw != 0))
      k = j - bdw;
  }

  c = 0.5 * (Get(i, i) + Get(j, j));
  Put(c, i, i);
  Put(c, j, j);

  c = 0.5 * (b[i] - b[j]);
  b[i] = c;
  b[j] = -c;

#ifdef KLUDGE
  bdw = tmpbdw;
#endif
}

void CBigLinProb::Periodicity(int i, int j)
{
  int k, fst, lst;
  double v1, v2, c;

#ifdef KLUDGE
  int tmpbdw = bdw;
  bdw = 0;
#endif

  if (j < i) {
    k = j;
    j = i;
    i = k;
  }

  if (bdw == 0) {
    fst = 0;
    lst = n;
  } else {
    fst = i - bdw;
    if (fst < 0)
      fst = 0;
    lst = j + bdw;
    if (lst > n)
      lst = n;
  }

  for (k = fst; k < lst; k++) {
    if ((k != i) && (k != j)) {
      v1 = Get(k, i);
      v2 = Get(k, j);
      if ((v1 != 0) || (v2 != 0)) {
        c = (v1 + v2) / 2.;
        Put(c, k, i);
        Put(c, k, j);
      }
    }
    if ((k == i + bdw) && (k < j - bdw) && (bdw != 0))
      k = j - bdw;
  }

  c = (Get(i, i) + Get(j, j)) / 2.;
  Put(c, i, i);
  Put(c, j, j);

  c = 0.5 * (b[i] + b[j]);
  b[i] = c;
  b[j] = c;

#ifdef KLUDGE
  bdw = tmpbdw;
#endif
}

// a diagnostic routine to check whether that the bandwidth of the
// constructed matrix is actually consistent with a priori bandwidth.
void CBigLinProb::ComputeBandwidth()
{
  CEntry* e;
  int k, bw, maxbw;

  for (maxbw = 0, k = 0; k < n; k++) {
    e = M[k];
    while (e->next != NULL)
      e = e->next;
    bw = e->c - k;
    if (bw > maxbw)
      maxbw = bw;
  }

  MsgBox("Assumed Bandwidth = %i\nActual Bandwidth = %i", bdw, maxbw);
}

// Function to write the matrix and rhs to a text file the Matlab can read.
// Although the matrix is symmetric, both sides are written out because Matlab
// doesn't really understand symmetric matrices. See:
// https://www.mathworks.com/help/matlab/ref/spconvert.html
// myFile should have a name like stiffness.dat
// load myFile in Matlab with the line 'load stiffness.dat;'
// convert it into a sparse matrix with 'A=spconvert(stiffness);'
int CBigLinProb::SaveMe(CString myFile)
{
  int i;
  CEntry* e;
  FILE* fp;

  if ((fp = fopen(myFile, "wt")) == NULL) {
    // couldn't open file
    return 0;
  }

  for (i = 0; i < n; i++) {
    e = M[i];
    do {
      fprintf(fp, "%i %i %.15g\n", i + 1, e->c + 1, e->x);
      fprintf(fp, "%i %i %.15g\n", e->c + 1, i + 1, e->x);
      e = e->next;
    } while (e != NULL);
  }

  fclose(fp);

  return 1;
}