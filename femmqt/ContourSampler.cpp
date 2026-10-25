#define _USE_MATH_DEFINES

#include "ContourSampler.h"

#include "FemmProblem.h"
#include "MeshSolution.h"
#include "SolutionField.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <complex>

// Added by Claude (Anthropic), noreply@anthropic.com, 2026-10-25 (#114).
// See ContourSampler.h.

namespace {

using Complex = std::complex<double>;
constexpr double kMuo = 1.2566370614359173e-6; // femm/StdAfx.h's muo

struct CVec {
  Complex x, y;
};

// A uniform grid of element bounding boxes, for point location.
class Locator {
  public:
  explicit Locator(const MeshSolution& s)
      : m_s(s)
  {
    if (s.nodes.isEmpty() || s.elements.isEmpty())
      return;
    m_x0 = m_x1 = s.nodes[0].x;
    m_y0 = m_y1 = s.nodes[0].y;
    for (const MeshSolutionNode& n : s.nodes) {
      m_x0 = std::min(m_x0, n.x);
      m_x1 = std::max(m_x1, n.x);
      m_y0 = std::min(m_y0, n.y);
      m_y1 = std::max(m_y1, n.y);
    }
    m_n = std::max(1, int(std::sqrt(double(s.elements.size()) / 4.)));
    m_cells.resize(m_n * m_n);
    for (int i = 0; i < s.elements.size(); i++) {
      const MeshSolutionElement& e = s.elements[i];
      double ex0 = 1e300, ex1 = -1e300, ey0 = 1e300, ey1 = -1e300;
      for (int k : { e.p0, e.p1, e.p2 }) {
        ex0 = std::min(ex0, s.nodes[k].x);
        ex1 = std::max(ex1, s.nodes[k].x);
        ey0 = std::min(ey0, s.nodes[k].y);
        ey1 = std::max(ey1, s.nodes[k].y);
      }
      for (int cx = cell(ex0, m_x0, m_x1); cx <= cell(ex1, m_x0, m_x1); cx++)
        for (int cy = cell(ey0, m_y0, m_y1); cy <= cell(ey1, m_y0, m_y1); cy++)
          m_cells[cy * m_n + cx].push_back(i);
    }
  }

  // The containing element and the barycentric weights, or -1.
  int find(double x, double y, double w[3]) const
  {
    if (m_cells.isEmpty() || x < m_x0 || x > m_x1 || y < m_y0 || y > m_y1)
      return -1;
    for (int i : m_cells[cell(y, m_y0, m_y1) * m_n + cell(x, m_x0, m_x1)]) {
      const MeshSolutionElement& e = m_s.elements[i];
      const MeshSolutionNode& a = m_s.nodes[e.p0];
      const MeshSolutionNode& b = m_s.nodes[e.p1];
      const MeshSolutionNode& c = m_s.nodes[e.p2];
      const double det = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
      if (det == 0)
        continue;
      w[0] = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / det;
      w[1] = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / det;
      w[2] = 1. - w[0] - w[1];
      if (w[0] >= -1e-12 && w[1] >= -1e-12 && w[2] >= -1e-12)
        return i;
    }
    return -1;
  }

  private:
  int cell(double v, double lo, double hi) const
  {
    if (hi <= lo)
      return 0;
    return std::clamp(int((v - lo) / (hi - lo) * m_n), 0, m_n - 1);
  }

  const MeshSolution& m_s;
  double m_x0 = 0, m_x1 = 0, m_y0 = 0, m_y1 = 0;
  int m_n = 0;
  QVector<QVector<int>> m_cells;
};

int materialOf(const FemmProblem& p, const MeshSolutionElement& e)
{
  return (e.lbl >= 0 && e.lbl < p.blockLabels.size()) ? p.blockLabels[e.lbl].blockTypeIndex : 0;
}

double elementArea(const MeshSolution& s, const MeshSolutionElement& e)
{
  const MeshSolutionNode& a = s.nodes[e.p0];
  const MeshSolutionNode& b = s.nodes[e.p1];
  const MeshSolutionNode& c = s.nodes[e.p2];
  return 0.5 * std::fabs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y));
}

// Per element corner, the area-weighted mean of `field` over the
// elements sharing that node AND that material -- smoothing must not
// blur across an iron/air boundary, where the field really jumps.
QVector<CVec> smoothCorners(const MeshSolution& s, const FemmProblem& p, CVec (*field)(const MeshSolutionElement&))
{
  QHash<quint64, CVec> sum;
  QHash<quint64, double> weight;
  auto key = [](int node, int mat) { return (quint64(quint32(node)) << 32) | quint32(mat); };
  for (const MeshSolutionElement& e : s.elements) {
    const double a = elementArea(s, e);
    const CVec f = field(e);
    const int m = materialOf(p, e);
    for (int n : { e.p0, e.p1, e.p2 }) {
      CVec& acc = sum[key(n, m)];
      acc.x += a * f.x;
      acc.y += a * f.y;
      weight[key(n, m)] += a;
    }
  }
  QVector<CVec> out(3 * s.elements.size());
  for (int i = 0; i < s.elements.size(); i++) {
    const MeshSolutionElement& e = s.elements[i];
    const int m = materialOf(p, e);
    const int corners[3] = { e.p0, e.p1, e.p2 };
    for (int k = 0; k < 3; k++) {
      const quint64 kk = key(corners[k], m);
      const double w = weight.value(kk);
      const CVec acc = sum.value(kk);
      out[3 * i + k] = w > 0 ? CVec { acc.x / w, acc.y / w } : field(e);
    }
  }
  return out;
}

CVec bField(const MeshSolutionElement& e)
{
  return { Complex(e.B1re, e.B1im), Complex(e.B2re, e.B2im) };
}

// The intensity is MINUS the stored gradient: classic's E = D/eps and
// G = F/k both point along the flux, i.e. -grad V and -grad T.
CVec gField(const MeshSolutionElement& e)
{
  return { -Complex(e.g1re, e.g1im), -Complex(e.g2re, e.g2im) };
}

// H from B in this element's material: B/(mu_r mu0) per axis for a
// linear one, the B-H curve at |B| (H parallel to B) for a nonlinear one
// -- classic's GetH for an unlaminated material.
CVec hFrom(const CVec& b, const MeshSolutionElement& e, const MeshSolution& s)
{
  if (e.bhMaterialIndex >= 0 && e.bhMaterialIndex < s.nonlinearMaterials.size()) {
    const double bMag = std::sqrt(std::norm(b.x) + std::norm(b.y));
    if (bMag <= 0)
      return {};
    const double hMag = BHCurve::interpolateH(s.nonlinearMaterials[e.bhMaterialIndex], bMag);
    return { b.x * (hMag / bMag), b.y * (hMag / bMag) };
  }
  return { b.x / (e.muX * kMuo), b.y / (e.muY * kMuo) };
}

double component(const CVec& v, double ux, double uy, bool ac)
{
  const Complex c = ux * v.x + uy * v.y;
  return ac ? std::abs(c) : c.real();
}

double magnitude(const CVec& v)
{
  return std::sqrt(std::norm(v.x) + std::norm(v.y));
}

} // namespace

QString ContourSampler::lengthUnitName(const FemmProblem& p)
{
  switch (p.lengthUnits) {
  case FemmLengthUnits::Millimeters: return QStringLiteral("mm");
  case FemmLengthUnits::Centimeters: return QStringLiteral("cm");
  case FemmLengthUnits::Meters: return QStringLiteral("m");
  case FemmLengthUnits::Mils: return QStringLiteral("mils");
  case FemmLengthUnits::Microns: return QStringLiteral("um");
  case FemmLengthUnits::Inches: return QStringLiteral("in");
  }
  return QString();
}

ContourSampler::Result ContourSampler::sample(const MeshSolution& s, const FemmProblem& p, FemmProblemKind kind,
    bool smooth, const QVector<QPointF>& contour, int samples)
{
  Result r;
  r.lengthUnit = lengthUnitName(p);
  if (contour.size() < 2 || samples < 1 || s.elements.isEmpty())
    return r;

  const bool magnetics = kind == FemmProblemKind::Magnetics;
  const bool ac = p.frequency != 0;
  const bool axi = p.problemType == FemmCoordinateType::Axisymmetric;
  const double lc = lengthToMeters(p.lengthUnits);

  // The series, named as classic names them.
  QVector<Series>& out = r.series;
  if (magnetics) {
    const QString pot = axi ? QStringLiteral("Flux") : QStringLiteral("A");
    const QString potUnit = axi ? QStringLiteral("Wb") : QStringLiteral("Wb/m");
    out << Series { ac ? QStringLiteral("|%1|").arg(pot) : pot, potUnit, {} }
        << Series { "|B|", "T", {} } << Series { "B.n", "T", {} } << Series { "B.t", "T", {} }
        << Series { "|H|", "A/m", {} } << Series { "H.n", "A/m", {} } << Series { "H.t", "A/m", {} };
    if (ac)
      out << Series { "|Je|", "MA/m^2", {} } << Series { "|Js+Je|", "MA/m^2", {} };
  } else {
    const QVector<SolutionField::Quantity> all = SolutionField::quantities(kind);
    const SolutionField::Quantity f = SolutionField::fieldQuantity(kind);
    const SolutionField::Quantity g = SolutionField::intensityQuantity(kind);
    out << Series { all.isEmpty() ? QStringLiteral("V") : all.first().symbol.isEmpty() ? all.first().name : all.first().symbol,
        all.isEmpty() ? QString() : all.first().unit, {} };
    if (out[0].label.isEmpty())
      out[0].label = SolutionField::potentialQuantity(kind).symbol;
    out << Series { QStringLiteral("|%1|").arg(f.symbol), f.unit, {} }
        << Series { QStringLiteral("%1.n").arg(f.symbol), f.unit, {} }
        << Series { QStringLiteral("%1.t").arg(f.symbol), f.unit, {} }
        << Series { QStringLiteral("|%1|").arg(g.symbol), g.unit, {} }
        << Series { QStringLiteral("%1.n").arg(g.symbol), g.unit, {} }
        << Series { QStringLiteral("%1.t").arg(g.symbol), g.unit, {} };
  }

  const QVector<CVec> smoothB = smooth ? smoothCorners(s, p, &bField) : QVector<CVec>();
  const QVector<CVec> smoothG = (smooth && !magnetics) ? smoothCorners(s, p, &gField) : QVector<CVec>();
  const Locator locate(s);

  QVector<double> segStart(contour.size(), 0.);
  for (int i = 1; i < contour.size(); i++)
    segStart[i] = segStart[i - 1] + std::hypot(contour[i].x() - contour[i - 1].x(), contour[i].y() - contour[i - 1].y());
  const double total = segStart.last();

  for (int i = 0; i <= samples; i++) {
    const double z = total * i / samples;
    int k = 1;
    while (k < contour.size() - 1 && z > segStart[k])
      k++;
    const double len = segStart[k] - segStart[k - 1];
    const double u = len > 0 ? (z - segStart[k - 1]) / len : 0.;
    const QPointF a = contour[k - 1], b = contour[k];
    double tx = b.x() - a.x(), ty = b.y() - a.y();
    const double tl = std::hypot(tx, ty);
    if (tl > 0) {
      tx /= tl;
      ty /= tl;
    }
    const double nx = -ty, ny = tx; // n = i*t
    const double px = a.x() + u * (b.x() - a.x()) + nx * 1.e-6;
    const double py = a.y() + u * (b.y() - a.y()) + ny * 1.e-6;

    r.arc << z;
    r.x << px;
    r.y << py;
    double w[3];
    const int ei = locate.find(px, py, w);
    r.inside << (ei >= 0);
    if (ei < 0) {
      for (Series& ser : out)
        ser.values << qQNaN();
      continue;
    }
    const MeshSolutionElement& e = s.elements[ei];
    const MeshSolutionNode* nd[3] = { &s.nodes[e.p0], &s.nodes[e.p1], &s.nodes[e.p2] };
    Complex pot = 0;
    for (int c = 0; c < 3; c++)
      pot += w[c] * Complex(nd[c]->Are, nd[c]->Aim);

    auto at = [&](const QVector<CVec>& corners, CVec (*field)(const MeshSolutionElement&)) {
      if (corners.isEmpty())
        return field(e);
      CVec v;
      for (int c = 0; c < 3; c++) {
        v.x += w[c] * corners[3 * ei + c].x;
        v.y += w[c] * corners[3 * ei + c].y;
      }
      return v;
    };

    const CVec B = at(smoothB, &bField);
    if (magnetics) {
      const CVec H = hFrom(B, e, s);
      out[0].values << (ac ? std::abs(pot) : pot.real());
      out[1].values << magnitude(B);
      out[2].values << component(B, nx, ny, ac);
      out[3].values << component(B, tx, ty, ac);
      out[4].values << magnitude(H);
      out[5].values << component(H, nx, ny, ac);
      out[6].values << component(H, tx, ty, ac);
      if (ac) {
        // Je = -i omega sigma A at the point, MA/m^2 (sigma in MS/m), with
        // GetJA's zeroing rules; Js+Je swaps the element-average eddy term
        // in jRe/jIm for this point's.
        double c = e.sigma;
        if (e.lbl >= 0 && e.lbl < p.blockLabels.size()) {
          const FemmBlockLabel& lbl = p.blockLabels[e.lbl];
          const int m = lbl.blockTypeIndex - 1;
          if (m >= 0 && m < p.materialProps.size() && p.materialProps[m].dLam != 0 && p.materialProps[m].lamType == 0)
            c = 0;
          if (std::abs(lbl.turns) > 1)
            c = 0;
        }
        const double omega = 2. * M_PI * p.frequency;
        auto aOf = [&](Complex a, double x) {
          if (!axi)
            return a;
          const double rr = x * lc;
          return std::fabs(x) < 1.e-6 ? Complex(0) : a / (2. * M_PI * rr);
        };
        const Complex aPoint = aOf(pot, px);
        Complex aAvg = 0;
        for (int cc = 0; cc < 3; cc++)
          aAvg += aOf(Complex(nd[cc]->Are, nd[cc]->Aim), nd[cc]->x) / 3.;
        const Complex jePoint = -Complex(0, 1) * omega * c * aPoint;
        const Complex jeAvg = -Complex(0, 1) * omega * c * aAvg;
        out[7].values << std::abs(jePoint);
        out[8].values << std::abs(Complex(e.jRe, e.jIm) - jeAvg + jePoint);
      }
    } else {
      const CVec G = at(smoothG, &gField);
      out[0].values << pot.real();
      out[1].values << magnitude(B);
      out[2].values << component(B, nx, ny, ac);
      out[3].values << component(B, tx, ty, ac);
      out[4].values << magnitude(G);
      out[5].values << component(G, nx, ny, ac);
      out[6].values << component(G, tx, ty, ac);
    }
  }
  return r;
}
