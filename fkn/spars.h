// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-08:
// added CBigLinProb::GPUAccel and PCGSolveGPU() (declared here, defined in
// spars.cpp) for the optional CUDA-accelerated linear solve; see
// spars_cuda.h/.cu.
// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-09:
// added CBigComplexLinProb::GPUAccel and PBCGSolveGPU() (declared here,
// defined in cspars.cpp) for the optional CUDA-accelerated harmonic
// (AC/eddy-current) solve; see spars_cuda.h/.cu's CudaPBCGSolve.

class CEntry {
  public:
  double x; // value stored in the entry
  int c; // column that the entry lives in
  CEntry* next; // pointer to next entry in row;
  CEntry();

  private:
};

#define LAMBDA 1.5

class CBigLinProb {
  public:
  // data members

  double* V; // solution
  double* P; // search direction;
  double* R; // residual;
  double* U; // A * P;
  double* Z;
  double* b; // RHS of linear equation
  CEntry** M; // pointer to list of matrix entries;
  int n; // dimensions of the matrix;
  int bdw; // Optional matrix bandwidth parameter;
  double Precision; // error tolerance for solution
  int GPUAccel; // 1 = try the CUDA-accelerated solve (see PCGSolveGPU), falling
                // back to the CPU path if unavailable or it fails; 0 = CPU only.

  // member functions

  CBigLinProb(); // constructor
  ~CBigLinProb(); // destructor
  int Create(int d, int bw); // initialize the problem
  void Put(double v, int p, int q);
  // use to create/set entries in the matrix
  double Get(int p, int q);
  void AddTo(double v, int p, int q);
  int PCGSolve(int flag); // flag==true if guess for V present;
  bool PCGSolveGPU(int flag); // GPU path used by PCGSolve when GPUAccel is set;
                               // returns false if unavailable/failed (caller
                               // falls back to the CPU solve).
  void MultPC(double* X, double* Y);
  void MultA(double* X, double* Y);
  void SetValue(int i, double x);
  void Periodicity(int i, int j);
  void AntiPeriodicity(int i, int j);
  void Wipe();
  double Dot(double* X, double* Y);
  void ComputeBandwidth();
  int SaveMe(CString myFile);
  CFknDlg* TheView;

  private:
};

/////////////////////////////////////////////////////////////////////
// for complex matrices......

class CComplexEntry {
  public:
  CComplex x; // value stored in the entry
  int c; // column that the entry lives in
  CComplexEntry* next; // pointer to next entry in the row;
  CComplexEntry();

  private:
};

class CBigComplexLinProb {
  public:
  // data members

  CComplex* P; // search direction
  CComplex* U;
  CComplex* R; // residual
  CComplex* V;
  CComplex* Z;
  CComplex* b; // RHS of linear equation
  CComplex* uu;
  CComplex* vv;

  CComplexEntry** M; // pointer to list of matrix entries;
  CComplexEntry** Mh; // Hermitian matrix arising from N-R algorithm;
  CComplexEntry** Ma; // Antihermitian matrix arising from N-R algorithm;
  CComplexEntry** Ms; // Additional complex-symmetric matrix arising from N-R algorithm;
  int n; // dimensions of the matrix;
  int bdw; // optional bandwidth parameter;
  int bNewton; // Flag which denotes whether or not there are entries in Mh or Ms;
  int NumNodes;
  double Precision;
  int GPUAccel; // 1 = try the CUDA-accelerated solve (see PBCGSolveGPU), falling
                // back to the CPU path if unavailable, bNewton, or it fails; 0 = CPU only.

  // member functions

  CBigComplexLinProb(); // constructor
  ~CBigComplexLinProb(); // destructor
  int Create(int d, int bw, int nodes); // initialize the problem
  void Put(CComplex v, int p, int q, int k = 0); // use to create/set entries in the matrix
  CComplex Get(int p, int q, int k = 0);
  void AddTo(CComplex v, int p, int q);
  void MultA(CComplex* X, CComplex* Y, int k = 0);
  void MultConjA(CComplex* X, CComplex* Y, int k = 0);
  CComplex Dot(CComplex* x, CComplex* y);
  CComplex ConjDot(CComplex* x, CComplex* y);
  void SetValue(int i, CComplex x);
  void Periodicity(int i, int j);
  void AntiPeriodicity(int i, int j);
  void Wipe();
  void MultPC(CComplex* X, CComplex* Y);
  void MultAPPA(CComplex* X, CComplex* Y);

  // flag==false initializes solution to zero
  // flag==true  starts from solution of previous call
  int PBCGSolveMod(int flag); // Precondition Biconjugate Gradient
  int PCGSQStart();
  int PBCGSolve(int flag);
  bool PBCGSolveGPU(); // GPU path used by PBCGSolveMod when GPUAccel is set;
                        // returns false if unavailable/failed (caller falls
                        // back to the CPU PBCGSolve). V is always used as
                        // the initial guess (see spars_cuda.h).
  int BiCGSTAB(int flag);
  int KludgeSolve(int flag);

  CFknDlg* TheView;

  private:
};
