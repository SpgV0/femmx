#define _USE_MATH_DEFINES

#include "SolutionIntegrals.h"

#include "FemmProblem.h"

#include <cmath>

namespace {

constexpr double kMu0 = 4.0e-7 * M_PI;
constexpr double kEps0 = 8.8541878128e-12;

struct Bary {
  bool inside = false;
  double w0 = 0, w1 = 0, w2 = 0;
};

Bary barycentric(const SolvedMesh& mesh, int elementIndex, double x, double y)
{
  Bary b;
  const SolutionElement& e = mesh.elements[elementIndex];
  const SolutionNode& a = mesh.nodes[e.p0];
  const SolutionNode& n1 = mesh.nodes[e.p1];
  const SolutionNode& n2 = mesh.nodes[e.p2];

  const double det = (n1.y - n2.y) * (a.x - n2.x) + (n2.x - n1.x) * (a.y - n2.y);
  if (det == 0.0)
    return b;

  b.w0 = ((n1.y - n2.y) * (x - n2.x) + (n2.x - n1.x) * (y - n2.y)) / det;
  b.w1 = ((n2.y - a.y) * (x - n2.x) + (a.x - n2.x) * (y - n2.y)) / det;
  b.w2 = 1.0 - b.w0 - b.w1;

  // A small negative tolerance so a point exactly on an edge lands in one
  // of the two elements sharing it rather than in neither -- which is
  // what a strict test does at every shared edge in the mesh.
  constexpr double kEdgeTol = -1e-12;
  b.inside = (b.w0 >= kEdgeTol && b.w1 >= kEdgeTol && b.w2 >= kEdgeTol);
  return b;
}

// Twice the signed area of an element, in model units squared.
double doubleArea(const SolvedMesh& mesh, int elementIndex)
{
  const SolutionElement& e = mesh.elements[elementIndex];
  const SolutionNode& a = mesh.nodes[e.p0];
  const SolutionNode& b = mesh.nodes[e.p1];
  const SolutionNode& c = mesh.nodes[e.p2];
  return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
}

int materialIndexFor(const SolvedMesh& mesh, const FemmProblem& p, int elementIndex)
{
  const int label = mesh.elements[elementIndex].label;
  if (label < 0 || label >= p.blockLabels.size())
    return -1;
  const int oneBased = p.blockLabels[label].blockTypeIndex;
  return (oneBased > 0) ? (oneBased - 1) : -1;
}

// Energy (or loss) density for one element, in J/m^3 or W/m^3.
double energyDensity(const SolvedMesh& mesh, const FemmProblem& p, int elementIndex)
{
  SolutionField::Vector2 f;
  if (!SolutionField::elementField(mesh, p, elementIndex, f))
    return 0.0;
  const int mat = materialIndexFor(mesh, p, elementIndex);

  switch (p.kind) {
  case FemmProblemKind::Magnetics: {
    // |B|^2 / (2 mu). muX/muY are RELATIVE, so the absolute permeability
    // is mu0 times that; a hole or an unassigned label is free space.
    double mur = 1.0;
    if (mat >= 0 && mat < p.materialProps.size())
      mur = (p.materialProps[mat].muX + p.materialProps[mat].muY) / 2.0;
    if (mur <= 0)
      mur = 1.0;
    const double b2 = f.xRe * f.xRe + f.xIm * f.xIm + f.yRe * f.yRe + f.yIm * f.yIm;
    return b2 / (2.0 * kMu0 * mur);
  }
  case FemmProblemKind::Electrostatics: {
    // D.E/2, and elementField already returned D. E = D / (eps0 er).
    double er = 1.0;
    if (mat >= 0 && mat < p.esMaterialProps.size())
      er = (p.esMaterialProps[mat].ex + p.esMaterialProps[mat].ey) / 2.0;
    if (er <= 0)
      er = 1.0;
    const double d2 = f.xRe * f.xRe + f.xIm * f.xIm + f.yRe * f.yRe + f.yIm * f.yIm;
    return d2 / (2.0 * kEps0 * er);
  }
  case FemmProblemKind::CurrentFlow: {
    // J.E/2, and elementField returned J. E = J / sigma.
    double sigma = 0.0;
    if (mat >= 0 && mat < p.cfMaterialProps.size())
      sigma = (p.cfMaterialProps[mat].ox + p.cfMaterialProps[mat].oy) / 2.0;
    if (sigma <= 0)
      return 0.0; // an insulator carries no current and dissipates nothing
    const double j2 = f.xRe * f.xRe + f.xIm * f.xIm + f.yRe * f.yRe + f.yIm * f.yIm;
    return j2 / (2.0 * sigma);
  }
  case FemmProblemKind::HeatFlow:
    // Heat flow is a steady transport problem, not a storage one: there
    // is no energy density to integrate. Reporting zero rather than
    // inventing one -- see blockEnergyQuantity, which says so.
    return 0.0;
  }
  return 0.0;
}

} // namespace

int SolutionIntegrals::elementAt(const SolvedMesh& mesh, double x, double y)
{
  for (int i = 0; i < mesh.elements.size(); i++) {
    const SolutionElement& e = mesh.elements[i];
    const int n = mesh.nodes.size();
    if (e.p0 < 0 || e.p0 >= n || e.p1 < 0 || e.p1 >= n || e.p2 < 0 || e.p2 >= n)
      continue;
    if (barycentric(mesh, i, x, y).inside)
      return i;
  }
  return -1;
}

SolutionIntegrals::PointValue SolutionIntegrals::pointValue(const SolvedMesh& mesh,
    const FemmProblem& p, double x, double y)
{
  PointValue out;
  const int i = elementAt(mesh, x, y);
  if (i < 0)
    return out;

  const Bary b = barycentric(mesh, i, x, y);
  const SolutionElement& e = mesh.elements[i];
  // The potential is linear across the element, so barycentric weights
  // interpolate it exactly -- this is not an approximation.
  out.potentialRe = b.w0 * mesh.nodes[e.p0].potentialRe
      + b.w1 * mesh.nodes[e.p1].potentialRe + b.w2 * mesh.nodes[e.p2].potentialRe;
  out.potentialIm = b.w0 * mesh.nodes[e.p0].potentialIm
      + b.w1 * mesh.nodes[e.p1].potentialIm + b.w2 * mesh.nodes[e.p2].potentialIm;

  SolutionField::elementField(mesh, p, i, out.field);
  out.element = i;
  out.ok = true;
  return out;
}

SolutionIntegrals::BlockResult SolutionIntegrals::blockIntegral(const SolvedMesh& mesh,
    const FemmProblem& p, const QVector<int>& labels)
{
  BlockResult r;
  const double lc = lengthToMeters(p.lengthUnits);
  double weightedPotential = 0;

  for (int i = 0; i < mesh.elements.size(); i++) {
    const SolutionElement& e = mesh.elements[i];
    const int n = mesh.nodes.size();
    if (e.p0 < 0 || e.p0 >= n || e.p1 < 0 || e.p1 >= n || e.p2 < 0 || e.p2 >= n)
      continue;
    if (!labels.isEmpty() && !labels.contains(e.label))
      continue;

    // Model units squared -> m^2. The sign of doubleArea depends on the
    // node ordering triangle happened to emit, and an area is positive.
    const double area = std::abs(doubleArea(mesh, i)) * 0.5 * lc * lc;
    if (area == 0.0)
      continue;

    r.area += area;
    r.energy += energyDensity(mesh, p, i) * area;
    const double avgP = (mesh.nodes[e.p0].potentialRe + mesh.nodes[e.p1].potentialRe
                            + mesh.nodes[e.p2].potentialRe)
        / 3.0;
    weightedPotential += avgP * area;
    r.elementCount++;
  }

  if (r.area > 0)
    r.averagePotential = weightedPotential / r.area;
  r.ok = r.elementCount > 0;
  return r;
}

SolutionIntegrals::LineResult SolutionIntegrals::lineIntegral(const SolvedMesh& mesh,
    const FemmProblem& p, const QVector<QPair<double, double>>& polyline)
{
  LineResult r;
  if (polyline.size() < 2)
    return r;

  const double lc = lengthToMeters(p.lengthUnits);

  for (int i = 0; i + 1 < polyline.size(); i++) {
    const double x0 = polyline[i].first, y0 = polyline[i].second;
    const double x1 = polyline[i + 1].first, y1 = polyline[i + 1].second;
    const double dx = x1 - x0, dy = y1 - y0;
    const double segLen = std::hypot(dx, dy);
    if (segLen == 0.0)
      continue;

    // Sample the field at the segment's midpoint. The field is constant
    // within an element, so for a segment lying inside one element this
    // is exact; for one crossing several it is a one-point rule, which
    // is what the classic post-processor's contour integral also does
    // per sub-segment.
    const PointValue mid = pointValue(mesh, p, (x0 + x1) / 2.0, (y0 + y1) / 2.0);
    r.length += segLen * lc;
    if (!mid.ok)
      continue;

    // Normal = direction rotated by -90 degrees: (dy, -dx). With a
    // counterclockwise contour that points outward, so a closed loop
    // around a source gives a positive total.
    const double nx = dy / segLen;
    const double ny = -dx / segLen;
    const double dl = segLen * lc;
    r.fluxRe += (mid.field.xRe * nx + mid.field.yRe * ny) * dl;
    r.fluxIm += (mid.field.xIm * nx + mid.field.yIm * ny) * dl;
  }

  r.ok = true;
  return r;
}

SolutionField::Quantity SolutionIntegrals::blockEnergyQuantity(FemmProblemKind kind)
{
  switch (kind) {
  case FemmProblemKind::Magnetics:
    return { QStringLiteral("Stored magnetic energy"), QStringLiteral("J/m") };
  case FemmProblemKind::Electrostatics:
    return { QStringLiteral("Stored electric energy"), QStringLiteral("J/m") };
  case FemmProblemKind::CurrentFlow:
    return { QStringLiteral("Resistive loss"), QStringLiteral("W/m") };
  case FemmProblemKind::HeatFlow:
    // Deliberately empty: a steady heat-flow solution has no energy
    // density to integrate, and offering a number here would invite
    // reading zero as a result rather than as "not applicable".
    return {};
  }
  return {};
}

SolutionField::Quantity SolutionIntegrals::lineFluxQuantity(FemmProblemKind kind)
{
  switch (kind) {
  case FemmProblemKind::Magnetics:
    return { QStringLiteral("Magnetic flux"), QStringLiteral("Wb/m") };
  case FemmProblemKind::Electrostatics:
    return { QStringLiteral("Electric flux"), QStringLiteral("C/m") };
  case FemmProblemKind::HeatFlow:
    return { QStringLiteral("Heat flow through the contour"), QStringLiteral("W/m") };
  case FemmProblemKind::CurrentFlow:
    return { QStringLiteral("Current through the contour"), QStringLiteral("A/m") };
  }
  return {};
}
