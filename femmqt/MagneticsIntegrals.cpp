#define _USE_MATH_DEFINES

#include "MagneticsIntegrals.h"

#include "FemmProblem.h"
#include "MeshSolution.h"

#include <QHash>

#include <algorithm>
#include <cmath>

// Added by Claude (Anthropic), noreply@anthropic.com, 2026-10-25
// (issue #108). See MagneticsIntegrals.h. Every function below names the
// classic routine it ports; where classic does something odd, the port
// does the same odd thing and says so, because the reference numbers are
// classic's.

namespace {

using Complex = MagneticsIntegrals::Complex;
const Complex I(0, 1);

// femm/StdAfx.h's muo, not a recomputed 4*pi*1e-7.
constexpr double kMuo = 1.2566370614359173e-6;

// --- BH curve: femm/Problem.cpp ---------------------------------------------

double curveH(const BHCurve::Curve& c, double b)
{
  return BHCurve::interpolateH(c, std::fabs(b));
}

// The derivative of curveH, for GetB's Newton iteration.
double curveDhDb(const BHCurve::Curve& c, double b)
{
  const int n = c.b.size();
  b = std::fabs(b);
  if (n == 0)
    return 0;
  if (b > c.b[n - 1])
    return c.slope[n - 1];
  for (int i = 0; i < n - 1; i++) {
    if (b >= c.b[i] && b <= c.b[i + 1]) {
      const double l = c.b[i + 1] - c.b[i];
      const double z = (b - c.b[i]) / l;
      return 6. * z * (z - 1.) * c.h[i] / l + (1. - 4. * z + 3. * z * z) * c.slope[i]
          + 6. * z * (1. - z) * c.h[i + 1] / l + z * (3. * z - 2.) * c.slope[i + 1];
    }
  }
  return c.slope[0];
}

// CMaterialProp::GetEnergy(double): the integral of H dB along the
// Hermite spline, closed form per segment.
double curveEnergy(const BHCurve::Curve& c, double x)
{
  const double b = std::fabs(x);
  const int n = c.b.size();
  double nrg = 0;
  if (n == 0)
    return 0;
  for (int i = 0; i < n - 1; i++) {
    const double b0 = c.b[i], h0 = c.h[i], b1 = c.b[i + 1], h1 = c.h[i + 1];
    const double dh0 = c.slope[i], dh1 = c.slope[i + 1];
    if (b >= b0 && b <= b1) {
      const double l = b1 - b0;
      const double z = (b - b0) / l;
      const double z2 = z * z;
      nrg += (dh0 * l * l * (6. + z * (-8. + 3. * z)) * z2) / 12. + (h0 * l * z * (2. + (-2. + z) * z2)) / 2.
          - (h1 * l * (-2. + z) * z2 * z) / 2. + (dh1 * l * l * (-4. + 3. * z) * z2 * z) / 12;
      return nrg;
    }
    nrg += ((b0 - b1) * ((b0 - b1) * (dh0 - dh1) - 6. * (h0 + h1))) / 12.;
  }
  const double h0 = c.h[n - 1], dh0 = c.slope[n - 1], b0 = c.b[n - 1];
  nrg += ((b - b0) * (b * dh0 - b0 * dh0 + 2 * h0)) / 2.;
  return nrg;
}

double curveCoEnergy(const BHCurve::Curve& c, double b)
{
  return std::fabs(b) * curveH(c, b) - curveEnergy(c, b);
}

// CMaterialProp::GetB: Newton on H(B) = hc from B = 0.
double curveB(const BHCurve::Curve& c, double hc)
{
  double b = 0, bo = 0;
  for (int k = 0; k < 200; k++) {
    bo = b;
    const double d = curveDhDb(c, bo);
    if (d == 0)
      break;
    b = bo + (hc - curveH(c, bo)) / d;
    if (std::fabs(b - bo) <= 1.e-8)
      break;
  }
  return b;
}

// --- femm/FemmviewDoc.cpp's polynomial element integrals ---------------------

} // namespace

Complex MagneticsIntegrals::plnInt(double a, const Complex u[3], const Complex v[3])
{
  const Complex z[3] = { 2. * u[0] + u[1] + u[2], u[0] + 2. * u[1] + u[2], u[0] + u[1] + 2. * u[2] };
  Complex x = 0;
  for (int i = 0; i < 3; i++)
    x += v[i] * z[i];
  return a * x / 12.;
}

Complex MagneticsIntegrals::axiInt(double a, const Complex u[3], const Complex v[3], const double r[3])
{
  double M[3][3];
  M[0][0] = 6. * r[0] + 2. * r[1] + 2. * r[2];
  M[0][1] = 2. * r[0] + 2. * r[1] + 1. * r[2];
  M[0][2] = 2. * r[0] + 1. * r[1] + 2. * r[2];
  M[1][1] = 2. * r[0] + 6. * r[1] + 2. * r[2];
  M[1][2] = 1. * r[0] + 2. * r[1] + 2. * r[2];
  M[2][2] = 2. * r[0] + 2. * r[1] + 6. * r[2];
  M[1][0] = M[0][1];
  M[2][0] = M[0][2];
  M[2][1] = M[1][2];
  Complex x = 0;
  for (int i = 0; i < 3; i++) {
    const Complex z = M[i][0] * u[0] + M[i][1] * u[1] + M[i][2] * u[2];
    x += v[i] * z;
  }
  return M_PI * a * x / 30.;
}

namespace MagneticsIntegrals {

BlockIntegrator::BlockIntegrator(const MeshSolution& solution, const FemmProblem& problem, const QSet<int>& labels)
    : m_solution(solution)
    , m_problem(problem)
    , m_labels(labels)
{
}

bool BlockIntegrator::prepare(QString& why)
{
  if (m_prepared) {
    why = m_prepareWhy;
    return m_prepareOk;
  }
  m_prepared = true;
  m_axi = m_problem.problemType == FemmCoordinateType::Axisymmetric;
  m_lc = lengthToMeters(m_problem.lengthUnits);
  m_depth = m_problem.depth * m_lc;
  m_freq = m_problem.frequency;
  const double omega = 2. * M_PI * m_freq;

  const int nn = m_solution.nodes.size();
  m_elements.resize(m_solution.elements.size());
  for (int i = 0; i < m_solution.elements.size(); i++) {
    const MeshSolutionElement& src = m_solution.elements[i];
    Element& e = m_elements[i];
    e.node[0] = src.p0;
    e.node[1] = src.p1;
    e.node[2] = src.p2;
    for (int k = 0; k < 3; k++) {
      if (e.node[k] < 0 || e.node[k] >= nn) {
        m_prepareWhy = why = QStringLiteral("The solution file has an element with a missing node.");
        return m_prepareOk = false;
      }
    }
    const MeshSolutionNode* n[3] = { &m_solution.nodes[e.node[0]], &m_solution.nodes[e.node[1]], &m_solution.nodes[e.node[2]] };
    e.label = src.lbl;
    if (e.label >= 0 && e.label < m_problem.blockLabels.size()) {
      const int m = m_problem.blockLabels[e.label].blockTypeIndex - 1;
      if (m >= 0 && m < m_problem.materialProps.size())
        e.material = m;
    }
    // ElmArea, in m^2.
    e.area = 0.5 * std::fabs((n[1]->x - n[0]->x) * (n[2]->y - n[0]->y) - (n[2]->x - n[0]->x) * (n[1]->y - n[0]->y)) * m_lc * m_lc;
    for (int k = 0; k < 3; k++)
      e.r[k] = n[k]->x * m_lc;
    e.R = (e.r[0] + e.r[1] + e.r[2]) / 3.;
    e.ctr = Complex((n[0]->x + n[1]->x + n[2]->x) / 3., (n[0]->y + n[1]->y + n[2]->y) / 3.) * m_lc;
    e.B1 = Complex(src.B1re, src.B1im);
    e.B2 = Complex(src.B2re, src.B2im);

    // GetJA: A per node first...
    for (int k = 0; k < 3; k++) {
      const Complex a(n[k]->Are, m_freq != 0 ? n[k]->Aim : 0.);
      if (!m_axi) {
        e.A[k] = a;
      } else {
        const double rn = n[k]->x * m_lc;
        e.A[k] = (std::fabs(rn / m_lc) < 1.e-06) ? Complex(0) : a / (2. * M_PI * rn);
      }
    }

    // ...then J, in MA/m^2 until the end, exactly as GetJA.
    Complex jsrc;
    double c = 0;
    if (e.material >= 0) {
      const FemmMaterialProp& mat = m_problem.materialProps[e.material];
      jsrc = Complex(mat.JsrcRe, m_freq != 0 ? mat.JsrcIm : 0.);
      c = mat.sigma;
      if (mat.dLam != 0 && mat.lamType == 0)
        c = 0;
    }
    // FillFactor > 0, i.e. |turns| > 1 for a non-wound material (see
    // GetFillFactor's defaults): a bundle of wires, no bulk eddy current.
    if (e.label >= 0 && e.label < m_problem.blockLabels.size() && std::abs(m_problem.blockLabels[e.label].turns) > 1)
      c = 0;
    Complex javg = jsrc;
    for (int k = 0; k < 3; k++)
      e.Jn[k] = jsrc;
    if (m_freq != 0) {
      for (int k = 0; k < 3; k++) {
        e.Jn[k] -= I * omega * c * e.A[k];
        javg -= I * omega * c * e.A[k] / 3.;
      }
    }
    const bool inCircuit = e.label >= 0 && e.label < m_problem.blockLabels.size()
        && m_problem.blockLabels[e.label].circuitIndex > 0 && e.label < m_solution.labelCircuits.size();
    if (inCircuit) {
      const MeshLabelCircuit& ci = m_solution.labelCircuits[e.label];
      if (ci.caseFlag == 0) {
        if (!m_axi) {
          for (int k = 0; k < 3; k++)
            e.Jn[k] -= c * ci.dVolts;
          javg -= c * ci.dVolts;
        } else {
          // rn here is in MODEL units, divided by the conversion in the
          // test -- classic's own expression, kept as written.
          const double r = e.ctr.real();
          for (int k = 0; k < 3; k++) {
            const double rn = n[k]->x;
            if (std::fabs(rn / m_lc) < 1.e-06)
              e.Jn[k] -= c * ci.dVolts / r;
            else
              e.Jn[k] -= c * ci.dVolts / (rn * m_lc);
          }
          javg -= c * ci.dVolts / r;
        }
      } else {
        for (int k = 0; k < 3; k++)
          e.Jn[k] += ci.J;
        javg += ci.J;
      }
    }
    for (int k = 0; k < 3; k++)
      e.Jn[k] *= 1.e06;
    e.J = javg * 1.e06;
  }
  return m_prepareOk = true;
}

double BlockIntegrator::aecf(const Element& e) const
{
  // AECF: the permeability correction for an axisymmetric external
  // (Kelvin) region. 1 everywhere else.
  if (!m_axi)
    return 1.;
  if (e.label < 0 || e.label >= m_problem.blockLabels.size() || !m_problem.blockLabels[e.label].isExternal)
    return 1.;
  const double r = std::abs(e.ctr / m_lc - I * m_problem.extZo);
  return (r * r * m_problem.extRi) / (m_problem.extRo * m_problem.extRo * m_problem.extRo);
}

bool BlockIntegrator::coversElement(const Element& e, int inttype, QString& why) const
{
  if (m_axi && e.label >= 0 && e.label < m_problem.blockLabels.size() && m_problem.blockLabels[e.label].isExternal
      && m_problem.extRo == 0) {
    why = QStringLiteral("An axisymmetric external region has no external-region radii in this file.");
    return false;
  }
  if (e.material < 0)
    return true; // nothing material-dependent to get wrong
  const FemmMaterialProp& mat = m_problem.materialProps[e.material];
  const bool usesJ = inttype == 0 || inttype == 4 || inttype == 6 || inttype == 7 || (inttype >= 11 && inttype <= 16);
  const bool usesMaterial = inttype == 2 || inttype == 3 || inttype == 6 || inttype == 17;
  if (mat.lamType >= 3 && (usesJ || usesMaterial)) {
    why = QStringLiteral("\"%1\" is a wound (stranded or litz) region; its fill-factor model is not "
                         "ported to this version, so this integral is not computed rather than "
                         "approximated. The classic GUI computes it.")
              .arg(mat.name);
    return false;
  }
  if (usesMaterial && !mat.bhData.isEmpty() && m_freq != 0) {
    why = QStringLiteral("\"%1\" is nonlinear and this is an AC solution; classic's effective AC "
                         "B-H curve is not ported, so energy and loss integrals are not computed "
                         "here. The classic GUI computes them.")
              .arg(mat.name);
    return false;
  }
  if (usesMaterial && m_freq != 0 && mat.lamType != 0) {
    why = QStringLiteral("\"%1\" is laminated parallel to an axis; its AC permeability is not "
                         "ported. The classic GUI computes this integral.")
              .arg(mat.name);
    return false;
  }
  if ((inttype == 2 || inttype == 6 || inttype == 17) && m_freq == 0 && !mat.bhData.isEmpty()) {
    const int bh = m_solution.elements.value(&e - m_elements.constData()).bhMaterialIndex;
    if (bh < 0 || bh >= m_solution.nonlinearMaterials.size()) {
      why = QStringLiteral("\"%1\" is a laminated nonlinear material, whose B-H handling is not "
                           "ported. The classic GUI computes this integral.")
                .arg(mat.name);
      return false;
    }
  }
  if (inttype == 2 && mat.Hc != 0 && e.label >= 0 && e.label < m_problem.blockLabels.size()
      && !m_problem.blockLabels[e.label].magDirFctn.isEmpty()) {
    why = QStringLiteral("A magnet's direction is given as a Lua expression; this version does "
                         "not evaluate it. The classic GUI computes this integral.");
    return false;
  }
  return true;
}

double BlockIntegrator::energyDC(const Element& e, bool coenergy, bool& ok) const
{
  // DoEnergy / DoCoEnergy(double, double), with BlockIntegral's own
  // permanent-magnet correction around it for the energy.
  ok = true;
  if (e.material < 0)
    return (std::norm(e.B1.real()) + std::norm(e.B2.real())) / (2. * kMuo);
  const FemmMaterialProp& mat = m_problem.materialProps[e.material];
  const double b1 = e.B1.real(), b2 = e.B2.real();
  const int bh = m_solution.elements[&e - m_elements.constData()].bhMaterialIndex;
  const BHCurve::Curve* curve = (bh >= 0 && bh < m_solution.nonlinearMaterials.size()) ? &m_solution.nonlinearMaterials[bh] : nullptr;
  const bool nonlinear = !mat.bhData.isEmpty();

  auto doEnergy = [&](double x1, double x2) -> double {
    if (!nonlinear) {
      const double lf = mat.lamFill;
      double h1 = 0, h2 = 0;
      if (mat.lamType == 0) {
        h1 = x1 / ((1. + lf * (mat.muX - 1.)) * kMuo);
        h2 = x2 / ((1. + lf * (mat.muY - 1.)) * kMuo);
      } else if (mat.lamType == 1) {
        h1 = x1 / ((1. + lf * (mat.muX - 1.)) * kMuo);
        h2 = x1 * (lf / (mat.muY * kMuo) + (1. - lf) / kMuo); // b1, as classic writes it
      } else if (mat.lamType == 2) {
        h2 = x1 / ((1. + lf * (mat.muY - 1.)) * kMuo); // likewise
        h1 = x1 * (lf / (mat.muX * kMuo) + (1. - lf) / kMuo);
      } else {
        h1 = x1 / kMuo;
        h2 = x2 / kMuo;
      }
      return (h1 * x1 + h2 * x2) / 2.;
    }
    if (!curve) {
      ok = false;
      return 0;
    }
    return coenergy ? curveCoEnergy(*curve, std::sqrt(x1 * x1 + x2 * x2)) : curveEnergy(*curve, std::sqrt(x1 * x1 + x2 * x2));
  };

  if (!coenergy && mat.Hc != 0) {
    const double md = (e.label >= 0 && e.label < m_problem.blockLabels.size()) ? m_problem.blockLabels[e.label].magDir : 0.;
    const Complex hc = mat.Hc * std::exp(I * M_PI * md / 180.);
    if (!nonlinear) {
      const double h1 = b1 / (mat.muX * kMuo) - hc.real();
      const double h2 = b2 / (mat.muY * kMuo) - hc.imag();
      return 0.5 * kMuo * (mat.muX * h1 * h1 + mat.muY * h2 * h2);
    }
    if (!curve) {
      ok = false;
      return 0;
    }
    // Nrg: the coenergy at the curve's own B for H = Hc (classic computes
    // it once at load, for H_c > 0 only).
    const double nrg = mat.Hc > 0 ? curveCoEnergy(*curve, curveB(*curve, mat.Hc)) : 0.;
    return doEnergy(b1, b2) + nrg - mat.Hc * ((Complex(b1, b2)) / std::exp(I * M_PI * md / 180.)).real();
  }
  return doEnergy(b1, b2);
}

bool BlockIntegrator::muAC(const Element& e, Complex& mu1, Complex& mu2) const
{
  // CMaterialProp::GetMu(CComplex...) for a linear, unwound material:
  // mu_fdx / mu_fdy as FemmviewDoc's loader computes them -- hysteresis
  // lag, and the lamination's own eddy-current reduction.
  if (e.material < 0) {
    mu1 = mu2 = 1.;
    return true;
  }
  const FemmMaterialProp& mat = m_problem.materialProps[e.material];
  if (!mat.bhData.isEmpty() || mat.lamType != 0)
    return false;
  const double w = 2. * M_PI * m_freq;
  const Complex deg45 = Complex(1, 1);
  auto one = [&](double mu, double theta) {
    Complex fd = mu * std::exp(-I * theta * M_PI / 180.);
    if (mat.dLam != 0) {
      const Complex halflag = std::exp(-I * theta * M_PI / 360.);
      if (mat.sigma != 0) {
        const double ds = std::sqrt(2. / (0.4 * M_PI * w * mat.sigma * mu));
        const Complex K = halflag * deg45 * mat.dLam * 0.001 / (2. * ds);
        fd = (fd * std::tanh(K) / K) * mat.lamFill + (1. - mat.lamFill);
      } else {
        fd = fd * mat.lamFill + (1. - mat.lamFill);
      }
    }
    return fd;
  };
  mu1 = one(mat.muX, mat.phiHx);
  mu2 = one(mat.muY, mat.phiHy);
  return true;
}

Complex BlockIntegrator::henrotteVector(int k) const
{
  const Element& e = m_elements[k];
  const MeshSolutionNode* n[3] = { &m_solution.nodes[e.node[0]], &m_solution.nodes[e.node[1]], &m_solution.nodes[e.node[2]] };
  const double b[3] = { n[1]->y - n[2]->y, n[2]->y - n[0]->y, n[0]->y - n[1]->y };
  const double c[3] = { n[2]->x - n[1]->x, n[0]->x - n[2]->x, n[1]->x - n[0]->x };
  const double da = b[0] * c[1] - b[1] * c[0];
  Complex v = 0;
  for (int i = 0; i < 3; i++)
    v -= m_mask[e.node[i]] * (b[i] + I * c[i]) / (da * m_lc); // grad
  return v;
}

bool BlockIntegrator::makeMask(QString& why)
{
  // MakeMask (femm/makemask.cpp), WeightingScheme 0 -- classic's default:
  // a Laplace solve that is 1 on the selection and 0 on every other
  // non-air region and the outer boundary, then thresholded at 0.5. The
  // stress tensor is integrated over the elements the threshold crosses.
  if (m_maskTried) {
    why = m_maskWhy;
    return m_maskOk;
  }
  m_maskTried = true;
  const int nn = m_solution.nodes.size();
  const int ne = m_elements.size();
  QVector<double> V(nn, -1.);

  // Node -> elements, for IsKosher.
  QVector<QVector<int>> conList(nn);
  for (int i = 0; i < ne; i++)
    for (int j = 0; j < 3; j++)
      conList[m_elements[i].node[j]].push_back(i);

  bool onAxis = false;
  if (m_axi) {
    for (int i = 0; i < ne && !onAxis; i++) {
      if (!m_labels.contains(m_elements[i].label))
        continue;
      for (int j = 0; j < 3; j++)
        if (m_solution.nodes[m_elements[i].node[j]].x < 1.e-6)
          onAxis = true;
    }
  }
  auto isKosher = [&](int k) {
    if (!m_axi || m_solution.nodes[k].x > 1.e-6)
      return true;
    int score = 0;
    for (int el : conList[k]) {
      for (int j = 0; j < 3; j++) {
        const int n = m_elements[el].node[j];
        if (n != k && m_solution.nodes[n].x < 1.e-6) {
          score++;
          if (score > 1)
            return false;
        }
      }
    }
    return true;
  };

  // Which labels are "not air": any material property beyond free space,
  // or a circuit.
  auto labelIsNotAir = [&](int lbl) {
    if (lbl < 0 || lbl >= m_problem.blockLabels.size())
      return false;
    const FemmBlockLabel& b = m_problem.blockLabels[lbl];
    if (b.circuitIndex > 0)
      return true;
    const int m = b.blockTypeIndex - 1;
    if (m < 0 || m >= m_problem.materialProps.size())
      return false;
    const FemmMaterialProp& p = m_problem.materialProps[m];
    return p.muX != 1 || p.muY != 1 || !p.bhData.isEmpty() || p.lamType != 0 || p.Hc != 0
        || p.JsrcRe != 0 || p.JsrcIm != 0 || p.sigma != 0 || p.phiH != 0 || p.phiHx != 0 || p.phiHy != 0;
  };

  // Exterior edges -- those belonging to one element only -- are zero.
  QHash<quint64, int> edgeCount;
  auto key = [](int a, int b) { return (quint64(std::min(a, b)) << 32) | quint64(std::max(a, b)); };
  for (int i = 0; i < ne; i++)
    for (int j = 0; j < 3; j++)
      edgeCount[key(m_elements[i].node[(j + 1) % 3], m_elements[i].node[(j + 2) % 3])]++;
  for (int i = 0; i < ne; i++) {
    for (int j = 0; j < 3; j++) {
      const int a = m_elements[i].node[(j + 1) % 3], b = m_elements[i].node[(j + 2) % 3];
      if (edgeCount.value(key(a, b)) != 1)
        continue;
      if (!onAxis || isKosher(a))
        V[a] = 0;
      if (!onAxis || isKosher(b))
        V[b] = 0;
    }
  }

  // Selection -> 1, other non-air -> 0, in element order (later wins,
  // as in classic's single pass).
  for (int i = 0; i < ne; i++) {
    const Element& e = m_elements[i];
    if (m_labels.contains(e.label)) {
      for (int j = 0; j < 3; j++)
        V[e.node[j]] = 1;
    } else if (labelIsNotAir(e.label)) {
      for (int j = 0; j < 3; j++)
        V[e.node[j]] = 0;
    }
  }

  // Point currents outside the selection must not disturb the force.
  for (const FemmNode& pn : m_problem.nodes) {
    if (pn.pointPropIndex <= 0)
      continue;
    for (int i = 0; i < nn; i++) {
      if (std::hypot(m_solution.nodes[i].x - pn.x, m_solution.nodes[i].y - pn.y) < 1.e-8) {
        if (V[i] < 0)
          V[i] = 0;
        break;
      }
    }
  }

  // Validity: a non-air region that is not selected must be entirely 0.
  for (int i = 0; i < ne; i++) {
    const Element& e = m_elements[i];
    if (m_labels.contains(e.label) || !labelIsNotAir(e.label))
      continue;
    int zeros = 0;
    for (int j = 0; j < 3; j++)
      if (V[e.node[j]] == 0)
        zeros++;
    if (zeros < 3) {
      m_maskWhy = why = QStringLiteral("The selected region is invalid. A valid selection "
                                       "cannot abut a region which is not free space.");
      return m_maskOk = false;
    }
  }

  // Assemble the weighted Laplacian over the free nodes and solve by
  // Jacobi-preconditioned conjugate gradients (classic: PCG).
  QVector<int> freeIndex(nn, -1);
  int nf = 0;
  for (int i = 0; i < nn; i++)
    if (V[i] < 0)
      freeIndex[i] = nf++;
  QVector<QHash<int, double>> K(nf);
  QVector<double> rhs(nf, 0.);
  for (int i = 0; i < ne; i++) {
    const Element& e = m_elements[i];
    const MeshSolutionNode* n[3] = { &m_solution.nodes[e.node[0]], &m_solution.nodes[e.node[1]], &m_solution.nodes[e.node[2]] };
    const double p[3] = { n[1]->y - n[2]->y, n[2]->y - n[0]->y, n[0]->y - n[1]->y };
    const double q[3] = { n[2]->x - n[1]->x, n[0]->x - n[2]->x, n[1]->x - n[0]->x };
    const double a = (p[0] * q[1] - p[1] * q[0]) / 2.; // model units, as classic
    if (a == 0)
      continue;
    double v = 0;
    const double maxArea = (e.label >= 0 && e.label < m_problem.blockLabels.size()) ? m_problem.blockLabels[e.label].maxArea : 0.;
    v = maxArea > 0 ? std::sqrt(maxArea) : std::sqrt(std::fabs(a));
    for (int j = 0; j < 3; j++) {
      const int fj = freeIndex[e.node[j]];
      if (fj < 0)
        continue;
      for (int k = 0; k < 3; k++) {
        const double m = v * (p[j] * p[k] + q[j] * q[k]) / a;
        const int fk = freeIndex[e.node[k]];
        if (fk >= 0)
          K[fj][fk] += m;
        else
          rhs[fj] -= m * V[e.node[k]];
      }
    }
  }
  QVector<double> x(nf, 0.5), r(nf), z(nf), pv(nf), Ap(nf), diag(nf, 1.);
  for (int i = 0; i < nf; i++) {
    const double d = K[i].value(i);
    diag[i] = d != 0 ? d : 1.;
  }
  auto mul = [&](const QVector<double>& in, QVector<double>& out) {
    for (int i = 0; i < nf; i++) {
      double s = 0;
      for (auto it = K[i].constBegin(); it != K[i].constEnd(); ++it)
        s += it.value() * in[it.key()];
      out[i] = s;
    }
  };
  mul(x, Ap);
  double rhsNorm = 0;
  for (int i = 0; i < nf; i++) {
    r[i] = rhs[i] - Ap[i];
    rhsNorm += rhs[i] * rhs[i];
  }
  rhsNorm = std::sqrt(rhsNorm);
  for (int i = 0; i < nf; i++)
    pv[i] = z[i] = r[i] / diag[i];
  double rz = 0;
  for (int i = 0; i < nf; i++)
    rz += r[i] * z[i];
  for (int it = 0; it < 10 * nf + 100; it++) {
    double rn = 0;
    for (int i = 0; i < nf; i++)
      rn += r[i] * r[i];
    if (std::sqrt(rn) <= 1e-12 * std::max(rhsNorm, 1e-300))
      break;
    mul(pv, Ap);
    double pAp = 0;
    for (int i = 0; i < nf; i++)
      pAp += pv[i] * Ap[i];
    if (pAp == 0)
      break;
    const double alpha = rz / pAp;
    for (int i = 0; i < nf; i++) {
      x[i] += alpha * pv[i];
      r[i] -= alpha * Ap[i];
      z[i] = r[i] / diag[i];
    }
    double rzNew = 0;
    for (int i = 0; i < nf; i++)
      rzNew += r[i] * z[i];
    const double beta = rzNew / rz;
    rz = rzNew;
    for (int i = 0; i < nf; i++)
      pv[i] = z[i] + beta * pv[i];
  }

  m_mask.resize(nn);
  for (int i = 0; i < nn; i++) {
    const double val = V[i] >= 0 ? V[i] : x[freeIndex[i]];
    m_mask[i] = val > 0.5 ? 1. : 0.;
  }
  return m_maskOk = true;
}

bool BlockIntegrator::integral(int inttype, Complex& out, QString& why)
{
  out = 0;
  if (!prepare(why))
    return false;
  if (inttype < 0 || inttype > 24) {
    why = QStringLiteral("No such integral.");
    return false;
  }
  if (inttype == 6) { // total losses
    Complex a, b;
    if (!integral(3, a, why) || !integral(4, b, why))
      return false;
    out = a + b;
    return true;
  }

  const bool mask = inttype >= 18 && inttype <= 23;
  if (mask && !makeMask(why))
    return false;

  const bool ac = m_freq != 0;
  const Complex U[3] = { 1., 1., 1. };
  Complex z = 0;

  for (int i = 0; i < m_elements.size(); i++) {
    const Element& e = m_elements[i];
    const bool selected = m_labels.contains(e.label);
    if (!(mask || selected))
      continue;
    if (!coversElement(e, inttype, why))
      return false;

    double a = e.area;
    const Complex& J = e.J;
    Complex y;

    if (mask) {
      a *= m_axi ? 2. * M_PI * e.R : m_depth;
      const Complex B1 = e.B1, B2 = e.B2;
      const Complex c = henrotteVector(i);
      const double f = aecf(e);
      switch (inttype) {
      case 18:
        if (m_axi)
          break;
        y = ((B1 * std::conj(B1) - B2 * std::conj(B2)) * c.real() + 2. * (B1 * std::conj(B2)).real() * c.imag()) / (2. * kMuo);
        if (ac)
          y /= 2.;
        z += a * y * f;
        break;
      case 19:
        y = ((B2 * std::conj(B2) - B1 * std::conj(B1)) * c.imag() + 2. * (B1 * std::conj(B2)).real() * c.real()) / (2. * kMuo);
        y *= f;
        if (ac)
          y /= 2.;
        z += a * y;
        break;
      case 20:
        if (m_axi)
          break;
        z += a * (((B1 * B1 - B2 * B2) * c.real() + 2. * B1 * B2 * c.imag()) / (4. * kMuo)) * f;
        break;
      case 21:
        z += a * (((B2 * B2 - B1 * B1) * c.imag() + 2. * B1 * B2 * c.real()) / (4. * kMuo)) * f;
        break;
      case 22: {
        if (m_axi)
          break;
        const Complex F1 = ((B1 * std::conj(B1) - B2 * std::conj(B2)) * c.real() + 2. * (B1 * std::conj(B2)).real() * c.imag()) / (2. * kMuo);
        const Complex F2 = ((B2 * std::conj(B2) - B1 * std::conj(B1)) * c.imag() + 2. * (B1 * std::conj(B2)).real() * c.real()) / (2. * kMuo);
        Complex cc = 0;
        for (int k = 0; k < 3; k++)
          cc += Complex(m_solution.nodes[e.node[k]].x, m_solution.nodes[e.node[k]].y) * m_lc / 3.;
        y = cc.real() * F2 - cc.imag() * F1;
        if (ac)
          y /= 2.;
        y *= f;
        z += a * y;
        break;
      }
      case 23: {
        if (m_axi)
          break;
        const Complex F1 = ((B1 * B1 - B2 * B2) * c.real() + 2. * B1 * B2 * c.imag()) / (4. * kMuo);
        const Complex F2 = ((B2 * B2 - B1 * B1) * c.imag() + 2. * B1 * B2 * c.real()) / (4. * kMuo);
        Complex cc = 0;
        for (int k = 0; k < 3; k++)
          cc += Complex(m_solution.nodes[e.node[k]].x, m_solution.nodes[e.node[k]].y) * m_lc / 3.;
        z += a * (cc.real() * F2 - cc.imag() * F1) * f;
        break;
      }
      default:
        break;
      }
      continue;
    }

    switch (inttype) {
    case 0: { // A.J
      Complex V[3];
      for (int k = 0; k < 3; k++)
        V[k] = std::conj(e.Jn[k]);
      z += m_axi ? axiInt(a, e.A, V, e.r) : plnInt(a, e.A, V) * m_depth;
      break;
    }
    case 1: // A
      if (m_axi) {
        z += axiInt(a, U, e.A, e.r);
      } else {
        for (int k = 0; k < 3; k++)
          z += a * m_depth * e.A[k] / 3.;
      }
      break;
    case 2:   // stored energy
    case 17: { // coenergy
      a *= m_axi ? 2. * M_PI * e.R : m_depth;
      if (ac) {
        Complex mu1, mu2;
        if (!muAC(e, mu1, mu2)) {
          why = QStringLiteral("This material's AC permeability is not ported.");
          return false;
        }
        const Complex h1 = e.B1 / (mu1 * kMuo), h2 = e.B2 / (mu2 * kMuo);
        y = a * (h1 * std::conj(e.B1) + h2 * std::conj(e.B2)).real() / 4.;
      } else {
        bool ok = true;
        y = a * energyDC(e, inttype == 17, ok);
        if (!ok) {
          why = QStringLiteral("This material's B-H curve was not loaded.");
          return false;
        }
      }
      z += y * aecf(e);
      break;
    }
    case 3: // hysteresis and laminated eddy-current losses
      if (ac) {
        a *= m_axi ? 2. * M_PI * e.R : m_depth;
        Complex mu1, mu2;
        if (!muAC(e, mu1, mu2)) {
          why = QStringLiteral("This material's AC permeability is not ported.");
          return false;
        }
        const double f = aecf(e);
        mu1 /= f;
        mu2 /= f;
        const Complex h1 = e.B1 / (mu1 * kMuo), h2 = e.B2 / (mu2 * kMuo);
        z += a * M_PI * m_freq * (h1 * std::conj(e.B1) + h2 * std::conj(e.B2)).imag();
      }
      break;
    case 4: { // resistive losses
      double sig = 0;
      if (e.material >= 0) {
        const FemmMaterialProp& mat = m_problem.materialProps[e.material];
        sig = mat.sigma * 1.e06; // 1e6 / Re(1/o), o = Cduct for an unwound material
        if (mat.dLam != 0 && mat.lamType == 0)
          sig = 0;
      }
      if (sig != 0) {
        if (!m_axi) {
          Complex V[3];
          for (int k = 0; k < 3; k++)
            V[k] = std::conj(e.Jn[k]) / sig;
          y = plnInt(a, e.Jn, V) * m_depth;
        } else {
          y = 2. * M_PI * e.R * a * J * std::conj(J) / sig;
        }
        if (ac)
          y /= 2.;
        z += y;
      }
      break;
    }
    case 5:
      z += a;
      break;
    case 7:
      z += a * J;
      break;
    case 8:
      z += a * (m_axi ? 2. * M_PI * e.R : m_depth) * e.B1;
      break;
    case 9:
      z += a * (m_axi ? 2. * M_PI * e.R : m_depth) * e.B2;
      break;
    case 10:
      z += a * (m_axi ? 2. * M_PI * e.R : m_depth);
      break;
    case 11: { // x (or r) Lorentz force, steady state
      const Complex B2 = e.B2;
      y = -(B2.real() * J.real() + B2.imag() * J.imag());
      if (m_axi)
        y = 0;
      else
        y *= m_depth;
      if (ac)
        y *= 0.5;
      z += a * y;
      break;
    }
    case 12: { // y (or z) Lorentz force, steady state
      Complex V[3];
      for (int k = 0; k < 3; k++)
        V[k] = (e.B1 * std::conj(e.Jn[k])).real();
      y = m_axi ? axiInt(-a, U, V, e.r) : plnInt(a, U, V) * m_depth;
      if (ac)
        y *= 0.5;
      z += y;
      break;
    }
    case 13: // x Lorentz force, 2x frequency
      if (ac && !m_axi) {
        const Complex B2 = e.B2;
        y = -(B2.real() * J.real() - B2.imag() * J.imag()) - I * (B2.real() * J.imag() + B2.imag() * J.real());
        z += 0.5 * (a * y * m_depth);
      }
      break;
    case 14: // y (or z) Lorentz force, 2x frequency
      if (ac) {
        const Complex B1 = e.B1;
        y = (B1.real() * J.real() - B1.imag() * J.imag()) + I * (B1.real() * J.imag() + B1.imag() * J.real());
        if (m_axi)
          y = (-y * 2. * M_PI * e.R);
        else
          y *= m_depth;
        z += (a * y) / 2.;
      }
      break;
    case 15: // Lorentz torque, steady state
      if (!m_axi) {
        const Complex B1 = e.B1, B2 = e.B2, c = e.ctr;
        y = c.imag() * (B2.real() * J.real() + B2.imag() * J.imag()) + c.real() * (B1.real() * J.real() + B1.imag() * J.imag());
        if (ac)
          y *= 0.5;
        z += a * y * m_depth;
      }
      break;
    case 16: // Lorentz torque, 2x frequency
      if (ac && !m_axi) {
        const Complex B1 = e.B1, B2 = e.B2, c = e.ctr;
        y = c.real() * ((B1.real() * J.real() - B1.imag() * J.imag()) + I * (B1.real() * J.imag() + B1.imag() * J.real()))
            + c.imag() * ((B2.real() * J.real() - B2.imag() * J.imag()) + I * (B2.real() * J.imag() + B2.imag() * J.real()));
        z += 0.5 * (a * y * m_depth);
      }
      break;
    case 24: // R^2 -- moment of inertia per density
      if (m_axi) {
        const Complex V[3] = { e.r[0], e.r[1], e.r[2] };
        z += axiInt(a, V, V, e.r);
      } else {
        double Ux[3], Vy[3];
        for (int k = 0; k < 3; k++) {
          Ux[k] = m_solution.nodes[e.node[k]].x * m_lc;
          Vy[k] = m_solution.nodes[e.node[k]].y * m_lc;
        }
        double s = Ux[0] * Ux[0] + Ux[1] * Ux[1] + Ux[2] * Ux[2] + Ux[0] * Ux[1] + Ux[0] * Ux[2] + Ux[1] * Ux[2];
        s += Vy[0] * Vy[0] + Vy[1] * Vy[1] + Vy[2] * Vy[2] + Vy[0] * Vy[1] + Vy[0] * Vy[2] + Vy[1] * Vy[2];
        z += s * (a * m_depth / 6.);
      }
      break;
    default:
      break;
    }
  }
  out = z;
  return true;
}

} // namespace MagneticsIntegrals
