#include <stdafx.h>
#include <math.h>
#include <stdio.h>
#include <vector>
#include "csolv.h"
#include "csolvDlg.h"
#include "complex.h"
#include "spars.h"
#include "spars_cuda.h"

// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
// PBCGSolveGPU() (CSR conversion + dispatch to the optional CUDA complex
// solver) added below PBCGSolveMod; PBCGSolveMod now tries the GPU path
// first when GPUAccel is set, falling back to the existing CPU path
// unchanged on any failure or when built without CUDA support. See
// spars_cuda.h for why this is its own kernel, not a reuse of fkn's
// CudaPBCGSolve (different convergence criterion).

#define KLUDGE

CComplexEntry::CComplexEntry()
{
  next = NULL;
  x = 0;
  c = 0;
}

CBigComplexLinProb::CBigComplexLinProb()
{
  n = 0;
  GPUAccel = 0;
}

CBigComplexLinProb::~CBigComplexLinProb()
{
  if (n == 0)
    return;

  int i;
  CComplexEntry *uo, *ui;

  free(b);
  free(P);
  free(R);
  free(V);
  free(U);
  free(Z);
  free(Q);

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

int CBigComplexLinProb::Create(int d, int bw, int nodes)
{
  int i;

  bdw = bw;
  NumNodes = nodes;
  b = (CComplex*)calloc(d, sizeof(CComplex));
  V = (CComplex*)calloc(d, sizeof(CComplex));
  P = (CComplex*)calloc(d, sizeof(CComplex));
  R = (CComplex*)calloc(d, sizeof(CComplex));
  U = (CComplex*)calloc(d, sizeof(CComplex));
  Z = (CComplex*)calloc(d, sizeof(CComplex));
  Q = (BOOL*)calloc(d, sizeof(BOOL));

  M = (CComplexEntry**)calloc(d, sizeof(CComplexEntry*));
  n = d;

  for (i = 0; i < d; i++) {
    M[i] = new CComplexEntry;
    M[i]->c = i;
  }

  return 1;
}

void CBigComplexLinProb::Put(CComplex v, int p, int q)
{
  CComplexEntry *e, *l;
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

  CComplexEntry* m = new CComplexEntry;

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

CComplex CBigComplexLinProb::Get(int p, int q)
{
  CComplexEntry* e;

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

  // if no entry in the list, this entry must be zero...
  return CComplex(0, 0);
}

void CBigComplexLinProb::MultA(CComplex* X, CComplex* Y)
{
  int i;
  CComplexEntry* e;

  for (i = 0; i < n; i++)
    Y[i] = 0;

  for (i = 0; i < n; i++) {
    Y[i] += (M[i]->x * X[i]);
    e = M[i]->next;
    while (e != NULL) {
      Y[i] += (e->x * X[e->c]);
      Y[e->c] += (e->x * X[i]);
      e = e->next;
    }
  }
}

void CBigComplexLinProb::MultConjA(CComplex* X, CComplex* Y)
{
  int i;
  CComplexEntry* e;

  for (i = 0; i < n; i++)
    Y[i] = 0;

  for (i = 0; i < n; i++) {
    Y[i] += (M[i]->x.Conj() * X[i]);
    e = M[i]->next;
    while (e != NULL) {
      Y[i] += (e->x.Conj() * X[e->c]);
      Y[e->c] += (e->x.Conj() * X[i]);
      e = e->next;
    }
  }
}

void CBigComplexLinProb::MultAPPA(CComplex* X, CComplex* Y)
{
  int i;
  MultA(X, Z);
  MultPC(Z, Y);
  for (i = 0; i < n; i++)
    Y[i].im = -Y[i].im;
  MultPC(Y, Z);
  MultA(Z, Y);
  for (i = 0; i < n; i++)
    Y[i].im = -Y[i].im;
}

CComplex CBigComplexLinProb::Dot(CComplex* x, CComplex* y)
{
  int i;
  CComplex z;

  z = 0;
  for (i = 0; i < n; i++)
    z += x[i] * y[i];

  return z;
}

CComplex CBigComplexLinProb::ConjDot(CComplex* x, CComplex* y)
{
  int i;
  CComplex z;

  z = 0;
  for (i = 0; i < n; i++)
    z += x[i].Conj() * y[i];

  return z;
}

void CBigComplexLinProb::MultPC(CComplex* X, CComplex* Y)
{
  /*	// Jacobi preconditioner:
    int i;
    for(i=0;i<n;i++) Y[i]=X[i]/M[i]->x; */

  // SSOR preconditioner
  int i;
  CComplex c;
  CComplexEntry* e;

  c = Lambda * (2 - Lambda);
  for (i = 0; i < n; i++)
    Y[i] = X[i] * c;

  // invert Lower Triangle;
  for (i = 0; i < n; i++) {
    Y[i] /= M[i]->x;
    e = M[i]->next;
    while (e != NULL) {
      Y[e->c] -= e->x * Y[i] * Lambda;
      e = e->next;
    }
  }

  for (i = 0; i < n; i++)
    Y[i] *= M[i]->x;

  // invert Upper Triangle
  for (i = n - 1; i >= 0; i--) {
    e = M[i]->next;
    while (e != NULL) {
      Y[i] -= e->x * Y[e->c] * Lambda;
      e = e->next;
    }
    Y[i] /= M[i]->x;
  }
}

int CBigComplexLinProb::PBCGSolve(int flag)
{
  int i;
  CComplex res, res_new, del, rho, pAp;
  double er, res_o;
  /*
    // quick check for most obvious sign of singularity;
    for(i=0;i<n;i++) if((M[i]->x.re==0) && (M[i]->x.im==0)){
      fprintf(stderr,"singular flag tripped.");
      return 0;
    }
  */
  // initialize progress bar;
  TheView->SetDlgItemText(IDC_FRAME1, "BiConjugate Gradient Solver");
  TheView->m_prog1.SetPos(0);
  int prg1 = 0;
  int prg2;

  // Guess for best relaxation parameter
  Lambda = 1.5;

  // residual with V=0
  MultPC(b, Z);
  res_o = abs(Dot(Z, b));
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

    er = sqrt(res.Abs() / res_o);

    // add a little bit more precision to account
    // for "bumpy" nature of BiCG
    prg2 = (int)(20. * log10(er) / (log10(Precision) - 2.));
    if (prg2 > prg1) {
      prg1 = prg2;
      prg2 = (prg1 * 5);
      if (prg2 > 100)
        prg2 = 100;
      TheView->m_prog1.SetPos(prg2);
      TheView->InvalidateRect(NULL, FALSE);
      TheView->UpdateWindow();
    }

  } while (er > (Precision * 0.01));

  return 1;
}

void CBigComplexLinProb::SetValue(int i, CComplex x)
{
  int k, fst, lst;
  CComplex z;

  if (bdw == 0) {
    fst = 0;
    lst = n;
  } else {
    fst = i - bdw;
    if (fst < 0)
      fst = 0;
    lst = i + bdw;
    if (lst > NumNodes)
      lst = NumNodes;
  }

  for (k = fst; k < n; k++) {
    if (k == lst)
      k = NumNodes;

    z = Get(k, i);
    if ((z.re != 0) || (z.im != 0)) {
      b[k] -= (z * x);
      if (i != k) {
        z = 0;
        Put(z, k, i);
      }
    }
  }
  b[i] = Get(i, i) * x;
}

void CBigComplexLinProb::Wipe()
{
  int i;
  CComplexEntry* e;

  for (i = 0; i < n; i++) {
    b[i] = 0;
    e = M[i];
    do {
      e->x = 0;
      e = e->next;
    } while (e != NULL);
  }
}

void CBigComplexLinProb::AntiPeriodicity(int i, int j)
{
  int k, fst, lst;
  CComplex v1, v2, c;

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
    if (lst > NumNodes - 1)
      lst = NumNodes - 1;
  }

  for (k = fst; k < n; k++) {
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
    else if (k == lst)
      k = NumNodes;
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

void CBigComplexLinProb::Periodicity(int i, int j)
{
  int k, fst, lst;
  CComplex v1, v2, c;

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
    if (lst > NumNodes - 1)
      lst = NumNodes - 1;
  }

  for (k = fst; k < n; k++) {
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
    else if (k == lst)
      k = NumNodes;
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

// Alternate solver that could be used for the heck of it
// The preconditioner hasn't been integrated with the QMR solver yet.
int CBigComplexLinProb::QMRSolve(int flag)
{
  int i;
  CComplex alpha, beta, tau, rho, theta, sigma, psi, z;
  double tau_o, er;
  /*
    // quick check for most obvious sign of singularity;
    for(i=0;i<n;i++) if((M[i]->x.re==0) && (M[i]->x.im==0)){
      fprintf(stderr,"singular flag tripped.");
      return 0;
    }
  */
  // initialize progress bar;
  TheView->SetDlgItemText(IDC_FRAME1, "QMR Solver");
  TheView->m_prog1.SetPos(0);
  int prg1 = 0;
  int prg2;

  // if flag is false, initialize V with zeros;
  if (flag == 0)
    for (i = 0; i < n; i++)
      V[i] = 0;

  // form residual and initial search direction;
  MultA(V, R);
  for (i = 0; i < n; i++) {
    R[i] = b[i] - R[i];
    P[i] = R[i];
    Z[i] = 0;
  }

  tau = ConjDot(R, R);
  rho = Dot(P, R);
  theta = 0;

  tau_o = Re(tau);

  // do iteration;
  int k = 0;
  do {

    // step 1)
    MultA(P, U);

    // step 2)
    sigma = Dot(P, U);

    // step 3)
    alpha = rho / sigma;
    for (i = 0; i < n; i++)
      R[i] = R[i] - alpha * U[i];

    // step 4)
    z = theta;
    theta = ConjDot(R, R) / tau;
    psi = 1 / (1 + theta);
    tau = tau * theta * psi;

    for (i = 0; i < n; i++) {
      Z[i] = Z[i] * psi * z + P[i] * psi * alpha;
      V[i] = V[i] + Z[i];
    }

    // step 7)
    z = rho;
    rho = Dot(R, R);
    beta = rho / z;
    for (i = 0; i < n; i++)
      P[i] = R[i] + P[i] * beta;

    er = sqrt(tau.Abs() / tau_o);

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

// Make into a Hermitian problem and solve.
// This ought to be slower than BiPCG, but the idea is to avoid
// the situations where BiPCG breaks down.
// This works, but to do the whole problem this way can be
// painfully slow in practice.
int CBigComplexLinProb::PCGSQSolve(int flag)
{
  int i;
  CComplex res, res_new, del, rho, pAp;
  double er, res_o;
  /*
    // quick check for most obvious sign of singularity;
    for(i=0;i<n;i++) if((M[i]->x.re==0) && (M[i]->x.im==0)){
      fprintf(stderr,"singular flag tripped.");
      return 0;
    }
  */
  // initialize progress bar;
  TheView->SetDlgItemText(IDC_FRAME1, "Conjugate Gradient Solver");
  TheView->m_prog1.SetPos(0);
  int prg1 = 0;
  int prg2;

  // Guess for best relaxation parameter
  Lambda = 1.0;

  // Operate on RHS to scale for squared problem
  MultPC(b, Z);
  for (i = 0; i < n; i++)
    Z[i].im = -Z[i].im;
  MultPC(Z, b);
  MultA(b, Z);
  for (i = 0; i < n; i++)
    b[i] = Z[i].Conj();

  // residual with V=0
  res_o = abs(ConjDot(b, b));
  if (res_o == 0)
    return 1;

  // if flag is false, initialize V with zeros;
  if (flag == 0)
    for (i = 0; i < n; i++)
      V[i] = 0;

  // form residual;
  MultAPPA(V, R);
  for (i = 0; i < n; i++)
    R[i] = b[i] - R[i];

  // form initial search direction
  for (i = 0; i < n; i++)
    P[i] = R[i];
  res = ConjDot(R, R);

  // do iteration;
  do {
    // step i)
    MultAPPA(P, U);
    pAp = ConjDot(P, U);
    del = res / pAp;

    // step ii)
    for (i = 0; i < n; i++)
      V[i] += (del * P[i]);

    // step iii)
    for (i = 0; i < n; i++)
      R[i] -= (del * U[i]);

    // step iv)
    res_new = ConjDot(R, R);
    rho = res_new / res;
    res = res_new;

    // step v)
    for (i = 0; i < n; i++)
      P[i] = R[i] + (rho * P[i]);

    er = sqrt(res.Abs() / res_o);

    // Display convergence results
    prg2 = (int)(20. * log10(er) / log10(Precision));
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

// Make into a Hermitian problem and solve.
// Just use for a few iterations to get a good starting point
// for the regular BiPCG, which can sometimes get initialized
// with a pathological starting point.
int CBigComplexLinProb::PCGSQStart(int flag)
{
  int i, k;
  CComplex res, res_new, del, rho, pAp;

  // quick check for most obvious sign of singularity;
  for (i = 0; i < n; i++)
    if ((M[i]->x.re == 0) && (M[i]->x.im == 0)) {
      fprintf(stderr, "singular flag tripped.");
      return 0;
    }

  // Guess for best relaxation parameter
  Lambda = 1.0;

  // Operate on RHS to scale for squared problem
  MultPC(b, Z);
  for (i = 0; i < n; i++)
    Z[i].im = -Z[i].im;
  MultPC(Z, P);
  MultA(P, Z);
  for (i = 0; i < n; i++)
    P[i] = Z[i].Conj();

  // initialize V with zeros;
  for (i = 0; i < n; i++)
    V[i] = 0;

  // form residual;
  MultAPPA(V, R);
  for (i = 0; i < n; i++)
    R[i] = P[i] - R[i];

  // form initial search direction
  for (i = 0; i < n; i++)
    P[i] = R[i];
  res = ConjDot(R, R);

  // do iteration;
  for (k = 0; k < 3; k++) {
    // step i)
    MultAPPA(P, U);
    pAp = ConjDot(P, U);
    del = res / pAp;

    // step ii)
    for (i = 0; i < n; i++)
      V[i] += (del * P[i]);

    // step iii)
    for (i = 0; i < n; i++)
      R[i] -= (del * U[i]);

    // step iv)
    res_new = ConjDot(R, R);
    rho = res_new / res;
    res = res_new;

    // step v)
    for (i = 0; i < n; i++)
      P[i] = R[i] + (rho * P[i]);
  }

  return 1;
}

// Slightly modified version of the preconditioned biconjugate gradient.
// This version calls PCGSQStart to do a small number of iterations,
// moving the starting point for PBCG away from the pathological starting
// points that can sometimes crop up.
int CBigComplexLinProb::PBCGSolveMod(int flag)
{
  int i;
  CComplex res, res_new, del, rho, pAp;
  double er;
  static double res_o;

  // initialize progress bar;
  TheView->m_prog1.SetPos(0);
  int prg1 = 0;
  int prg2;

  // get starting point; singularity check;
  if (flag == FALSE) {
    TheView->SetDlgItemText(IDC_FRAME1, "Initializing Solver");
    if (PCGSQStart(flag) == 0)
      return 0;
  }
  TheView->SetDlgItemText(IDC_FRAME1, "BiConjugate Gradient Solver");

  // Guess for best relaxation parameter
  Lambda = 1.5;

  // residual with V from PCGSQ
  // if flag==TRUE, carry residual target over from the last iteration.
  if (flag == FALSE) {
    MultPC(V, Z);
    res_o = abs(Dot(Z, V));
    if (res_o == 0)
      return 1;
  }

  if (GPUAccel) {
#ifndef FEMM_CUDA_ENABLED
    MsgBox(
        "GPU acceleration was requested for this problem, but this copy of "
        "csolv.exe was built without CUDA support.\n\n"
        "To use GPU acceleration:\n"
        "1. Install the NVIDIA CUDA Toolkit (a version supported by your GPU "
        "driver -- run \"nvidia-smi\" to check the maximum CUDA version it "
        "supports).\n"
        "2. Rebuild femmx with CMake options -DENABLE_CUDA_SOLVER=ON "
        "-DFEMM_CUDA_ROOT=\"<path to CUDA Toolkit>\" (add "
        "-DFEMM_CUDA_CCBIN=\"<path to an nvcc-compatible MSVC toolset bin "
        "dir>\" too if your installed Visual Studio is newer than the CUDA "
        "Toolkit supports -- this is common; see csolv/CMakeLists.txt).\n"
        "3. See csolv/spars_cuda.cu and csolv/CMakeLists.txt for details.\n\n"
        "Continuing with the CPU solver for this run.");
#else
    if (TheView != NULL)
      TheView->SetDlgItemText(IDC_FRAME1, "BiConjugate Gradient Solver (GPU)");
    if (PBCGSolveGPU(res_o))
      return 1;
    MsgBox(
        "GPU-accelerated solve did not produce a result (see the "
        "console/stderr output for the exact reason).\n\n"
        "This usually means one of:\n"
        "- No CUDA-capable NVIDIA GPU is present on this machine, or\n"
        "- The installed NVIDIA driver is older than this build's CUDA "
        "Toolkit requires -- run \"nvidia-smi\" to check the maximum CUDA "
        "version your driver supports, then either update the driver or "
        "rebuild csolv.exe with an older -DFEMM_CUDA_ROOT to match it, or\n"
        "- The GPU solve ran but did not converge: the Jacobi preconditioner "
        "used for GPU parallelism is weaker than the CPU's SSOR, and some "
        "problems need more iterations than it can provide. This isn't a "
        "GPU/driver problem -- try the CPU solver for this specific problem "
        "instead.\n\n"
        "Continuing with the CPU solver for this run.");
#endif
    // GPU path unavailable, not built with CUDA support, or failed to
    // converge/errored out -- fall through to the CPU solve below rather
    // than failing the whole analysis.
  }

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

    er = sqrt(res.Abs() / res_o);

    // add a little bit more precision to account
    // for "bumpy" nature of BiCG
    prg2 = (int)(20. * log10(er) / (log10(Precision) - 2.));
    if (prg2 > prg1) {
      prg1 = prg2;
      prg2 = (prg1 * 5);
      if (prg2 > 100)
        prg2 = 100;
      TheView->m_prog1.SetPos(prg2);
      TheView->InvalidateRect(NULL, FALSE);
      TheView->UpdateWindow();
    }

  } while (er > (Precision * 0.01));

  return 1;
}

// Converts the upper-triangle-only linked-list matrix storage (M[]) into a
// full symmetric CSR matrix of cuDoubleComplex values and dispatches to
// the CUDA solver, passing through the already-computed res_o and the
// exact same (Precision * 0.01) tolerance PBCGSolveMod's own CPU loop
// checks against. Returns false (never throws/errors out loud) if this
// build wasn't compiled with CUDA support, or if the GPU solve itself
// fails for any reason -- either way the caller (PBCGSolveMod) falls back
// to the CPU path.
bool CBigComplexLinProb::PBCGSolveGPU(double res_o)
{
#ifndef FEMM_CUDA_ENABLED
  (void)res_o;
  return false;
#else
  int i;
  CComplexEntry* e;

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
  std::vector<cuDoubleComplex> values(nnz);
  std::vector<cuDoubleComplex> diag(n);
  std::vector<int> cursor(rowPtr.begin(), rowPtr.end() - 1);

  for (i = 0; i < n; i++) {
    for (e = M[i]; e != NULL; e = e->next) {
      colInd[cursor[i]] = e->c;
      values[cursor[i]] = make_cuDoubleComplex(e->x.re, e->x.im);
      cursor[i]++;
      if (e->c != i) {
        colInd[cursor[e->c]] = i;
        values[cursor[e->c]] = make_cuDoubleComplex(e->x.re, e->x.im);
        cursor[e->c]++;
      }
    }
    diag[i] = make_cuDoubleComplex(M[i]->x.re, M[i]->x.im); // M[i] is always the diagonal entry (see Create())
  }

  // CComplex (complex.h) and cuDoubleComplex are both plain {double, double}
  // pairs with no vtable -- safe to reinterpret the contiguous b/V arrays
  // directly rather than copying through an intermediate buffer.
  int iters = 0;
  int ok = CudaPBCGSolveMod(n, rowPtr.data(), colInd.data(), values.data(), nnz,
      diag.data(),
      reinterpret_cast<const cuDoubleComplex*>(b),
      reinterpret_cast<cuDoubleComplex*>(V),
      res_o, /*tolerance=*/Precision * 0.01, /*maxiter=*/100000, &iters);

  return ok != 0;
#endif
}
