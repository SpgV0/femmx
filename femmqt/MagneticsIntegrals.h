#pragma once

// MagneticsIntegrals.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-10-25
// (issue #108).
//
// Classic's magnetics block integrals -- femm/FemmviewDoc.cpp's
// BlockIntegral(inttype) -- for the solution femmqt has loaded. The Area
// Properties dialog offered 17 of them and computed four; the other 13
// (energy, coenergy, losses, Lorentz and weighted-stress-tensor force
// and torque, A.J, A, R^2) said "not available".
//
// A PORT, case by case, of classic's own formulas -- including its
// per-node current density (GetJA), its polynomial element integrals
// (PlnInt/AxiInt), its energy routines (DoEnergy/DoCoEnergy, BH curve
// included), its axisymmetric external-region correction (AECF) and the
// Laplace-solved mask behind the stress tensor (MakeMask,
// HenrotteVector). Every number tst_magnetics_integrals.cpp expects was
// produced by classic on the same .ans; none is a re-derivation.
//
// What is NOT ported is refused by name rather than approximated: AC
// with nonlinear materials, wound (litz/stranded) regions, nonlinear
// laminated materials, magnetisation directions given as Lua functions,
// and incremental-permeability solutions. A plausible number there would
// be worse than none.
//
// `inttype` is classic's own numbering, 0..24 (see the switch in
// BlockIntegral); the dialog maps its 17 entries onto it exactly the way
// femm/FemmviewView.cpp does.

#include <QSet>
#include <QString>
#include <QVector>

#include <complex>

struct FemmProblem;
struct MeshSolution;

namespace MagneticsIntegrals {

using Complex = std::complex<double>;

class BlockIntegrator {
  public:
  // `labels` is the selection: block-label indices, as the Areas tool
  // collects them.
  BlockIntegrator(const MeshSolution& solution, const FemmProblem& problem, const QSet<int>& labels);

  // Classic's BlockIntegral(inttype). False, with `why` saying what is
  // not covered, when this port declines -- or, for the stress-tensor
  // types (18..23), when classic's MakeMask would reject the selection
  // because it touches a region that is not free space.
  bool integral(int inttype, Complex& out, QString& why);

  private:
  struct Element {
    int node[3];
    int label = -1;
    int material = -1; // index into FemmProblem::materialProps, or -1
    double area = 0;   // m^2
    double r[3] = { 0, 0, 0 }; // node radius, m (axisymmetric)
    double R = 0;      // centroid radius, m
    Complex A[3];      // classic's A per node (divided by 2 pi r when axisymmetric)
    Complex Jn[3];     // A/m^2 per node
    Complex J;         // A/m^2, element average
    Complex B1, B2;    // T
    Complex ctr;       // centroid, m
  };

  bool prepare(QString& why);
  bool makeMask(QString& why);
  bool coversElement(const Element& e, int inttype, QString& why) const;
  double aecf(const Element& e) const;
  Complex henrotteVector(int elementIndex) const;
  double energyDC(const Element& e, bool coenergy, bool& ok) const;
  bool muAC(const Element& e, Complex& mu1, Complex& mu2) const;

  const MeshSolution& m_solution;
  const FemmProblem& m_problem;
  QSet<int> m_labels;
  bool m_prepared = false;
  bool m_prepareOk = false;
  QString m_prepareWhy;
  QVector<Element> m_elements;
  bool m_axi = false;
  double m_lc = 1;     // model length unit -> m
  double m_depth = 1;  // m
  double m_freq = 0;
  // Mask: computed once per selection (classic caches it the same way).
  bool m_maskTried = false;
  bool m_maskOk = false;
  QString m_maskWhy;
  QVector<double> m_mask;
};

// femm/FemmviewDoc.cpp's PlnInt and AxiInt: exact integrals of the
// product of two linear fields over a triangle, planar and revolved.
// Exposed for the test, which checks them against closed forms.
Complex plnInt(double a, const Complex u[3], const Complex v[3]);
Complex axiInt(double a, const Complex u[3], const Complex v[3], const double r[3]);

} // namespace MagneticsIntegrals
