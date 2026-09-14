#define _USE_MATH_DEFINES

#include "SolutionField.h"

#include "FemmProblem.h"

#include <cmath>

namespace {

// Vacuum permittivity, F/m. Electrostatics stores RELATIVE permittivity,
// so D = eps0 * er * E.
constexpr double kEps0 = 8.8541878128e-12;

// Material constants come from the element's block label, which is a
// 1-based index into the kind's own material list (0 or less meaning the
// label has no material -- a hole). Returning the free-space default
// rather than zero for an unassigned label keeps a hole from showing as
// an infinitely conductive or infinitely insulating region; it shows as
// air, which is what a hole is.
int materialIndexFor(const SolvedMesh& mesh, const FemmProblem& p, int elementIndex)
{
  if (elementIndex < 0 || elementIndex >= mesh.elements.size())
    return -1;
  const int label = mesh.elements[elementIndex].label;
  if (label < 0 || label >= p.blockLabels.size())
    return -1;
  const int oneBased = p.blockLabels[label].blockTypeIndex;
  return (oneBased > 0) ? (oneBased - 1) : -1;
}

} // namespace

double SolutionField::Vector2::magnitude() const
{
  return std::hypot(std::hypot(xRe, xIm), std::hypot(yRe, yIm));
}

// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13 (#87).
// Must agree with elementField() above, kind for kind.
SolutionField::Quantity SolutionField::fieldQuantity(FemmProblemKind kind)
{
  switch (kind) {
  case FemmProblemKind::Magnetics:
    return { QStringLiteral("Flux density |B|"), QStringLiteral("T"),
      QStringLiteral("B") };
  case FemmProblemKind::Electrostatics:
    // D, not E -- elementField multiplies through by eps0*er.
    return { QStringLiteral("Flux density |D|"), QStringLiteral("C/m^2"),
      QStringLiteral("D") };
  case FemmProblemKind::HeatFlow:
    return { QStringLiteral("Heat flux |F|"), QStringLiteral("W/m^2"),
      QStringLiteral("F") };
  case FemmProblemKind::CurrentFlow:
    return { QStringLiteral("Current density |J|"), QStringLiteral("A/m^2"),
      QStringLiteral("J") };
  }
  return {};
}

QVector<SolutionField::Quantity> SolutionField::quantities(FemmProblemKind kind)
{
  switch (kind) {
  case FemmProblemKind::Magnetics:
    return {
      { QStringLiteral("Flux density |B|"), QStringLiteral("T") },
      { QStringLiteral("Field intensity |H|"), QStringLiteral("A/m") },
      { QStringLiteral("Vector potential A"), QStringLiteral("Wb/m") },
      { QStringLiteral("Current density |J|"), QStringLiteral("MA/m^2") },
    };
  case FemmProblemKind::Electrostatics:
    return {
      { QStringLiteral("Voltage V"), QStringLiteral("V") },
      { QStringLiteral("Field intensity |E|"), QStringLiteral("V/m") },
      { QStringLiteral("Flux density |D|"), QStringLiteral("C/m^2") },
    };
  case FemmProblemKind::HeatFlow:
    return {
      { QStringLiteral("Temperature T"), QStringLiteral("K") },
      { QStringLiteral("Heat flux |F|"), QStringLiteral("W/m^2") },
      { QStringLiteral("Temperature gradient |G|"), QStringLiteral("K/m") },
    };
  case FemmProblemKind::CurrentFlow:
    return {
      { QStringLiteral("Voltage V"), QStringLiteral("V") },
      { QStringLiteral("Current density |J|"), QStringLiteral("A/m^2") },
      { QStringLiteral("Field intensity |E|"), QStringLiteral("V/m") },
    };
  }
  return {};
}

SolutionField::Quantity SolutionField::potentialQuantity(FemmProblemKind kind)
{
  switch (kind) {
  case FemmProblemKind::Magnetics:
    return { QStringLiteral("A"), QStringLiteral("Wb/m"), QStringLiteral("A") };
  case FemmProblemKind::Electrostatics:
  case FemmProblemKind::CurrentFlow:
    return { QStringLiteral("V"), QStringLiteral("V"), QStringLiteral("V") };
  case FemmProblemKind::HeatFlow:
    return { QStringLiteral("T"), QStringLiteral("K"), QStringLiteral("T") };
  }
  return {};
}

bool SolutionField::potentialGradient(const SolvedMesh& mesh, const FemmProblem& p,
    int elementIndex, Vector2& gradient)
{
  gradient = Vector2();
  if (elementIndex < 0 || elementIndex >= mesh.elements.size())
    return false;
  const SolutionElement& e = mesh.elements[elementIndex];
  const int n = mesh.nodes.size();
  if (e.p0 < 0 || e.p0 >= n || e.p1 < 0 || e.p1 >= n || e.p2 < 0 || e.p2 >= n)
    return false;

  const SolutionNode& a = mesh.nodes[e.p0];
  const SolutionNode& b = mesh.nodes[e.p1];
  const SolutionNode& c = mesh.nodes[e.p2];

  // Standard linear-triangle shape-function derivatives, the same
  // b[]/c[]/da the classic GetElementB builds.
  const double bb[3] = { b.y - c.y, c.y - a.y, a.y - b.y };
  const double cc[3] = { c.x - b.x, a.x - c.x, b.x - a.x };
  const double da = bb[0] * cc[1] - bb[1] * cc[0];

  // A zero-area element is degenerate; triangle can emit them at sharp
  // corners. Dividing here would put a NaN into the plot's auto-range
  // and flatten the entire legend, which looks like a solver failure
  // rather than one bad triangle.
  if (da == 0.0)
    return false;

  const double lc = lengthToMeters(p.lengthUnits);
  const double re[3] = { a.potentialRe, b.potentialRe, c.potentialRe };
  const double im[3] = { a.potentialIm, b.potentialIm, c.potentialIm };

  for (int i = 0; i < 3; i++) {
    gradient.xRe += re[i] * bb[i] / (da * lc);
    gradient.xIm += im[i] * bb[i] / (da * lc);
    gradient.yRe += re[i] * cc[i] / (da * lc);
    gradient.yIm += im[i] * cc[i] / (da * lc);
  }
  return true;
}

bool SolutionField::elementField(const SolvedMesh& mesh, const FemmProblem& p,
    int elementIndex, Vector2& field)
{
  field = Vector2();
  Vector2 g;
  if (!potentialGradient(mesh, p, elementIndex, g))
    return false;

  const int mat = materialIndexFor(mesh, p, elementIndex);

  switch (p.kind) {

  case FemmProblemKind::Magnetics:
    // B = curl A for A along z: (dA/dy, -dA/dx). Note this is the one
    // physics whose field is a CURL rather than a negative gradient, so
    // its components are swapped and only one is negated -- writing it
    // as -grad would give a field rotated by 90 degrees, which looks
    // entirely plausible on a plot.
    field.xRe = g.yRe;
    field.xIm = g.yIm;
    field.yRe = -g.xRe;
    field.yIm = -g.xIm;
    return true;

  case FemmProblemKind::Electrostatics: {
    // E = -grad V, then D = eps0 * er * E. Permittivity is anisotropic
    // in the format, so x and y scale independently.
    const double exr = (mat >= 0 && mat < p.esMaterialProps.size())
        ? p.esMaterialProps[mat].ex
        : 1.0;
    const double eyr = (mat >= 0 && mat < p.esMaterialProps.size())
        ? p.esMaterialProps[mat].ey
        : 1.0;
    field.xRe = -g.xRe * kEps0 * exr;
    field.xIm = -g.xIm * kEps0 * exr;
    field.yRe = -g.yRe * kEps0 * eyr;
    field.yIm = -g.yIm * kEps0 * eyr;
    return true;
  }

  case FemmProblemKind::HeatFlow: {
    // F = -k grad T. Thermal conductivity is already W/(m*K) in the
    // format, so no scaling beyond the material value itself.
    const double kx = (mat >= 0 && mat < p.htMaterialProps.size())
        ? p.htMaterialProps[mat].Kx
        : 0.0;
    const double ky = (mat >= 0 && mat < p.htMaterialProps.size())
        ? p.htMaterialProps[mat].Ky
        : 0.0;
    field.xRe = -g.xRe * kx;
    field.xIm = -g.xIm * kx;
    field.yRe = -g.yRe * ky;
    field.yIm = -g.yIm * ky;
    return true;
  }

  case FemmProblemKind::CurrentFlow: {
    // J = sigma * E = -sigma grad V. Conductivity is S/m in .fec (<ox>,
    // <oy>) -- NOT the MS/m that magnetics' <Sigma> uses, which is the
    // kind of difference that turns a current density into one a million
    // times too small without looking wrong.
    const double ox = (mat >= 0 && mat < p.cfMaterialProps.size())
        ? p.cfMaterialProps[mat].ox
        : 0.0;
    const double oy = (mat >= 0 && mat < p.cfMaterialProps.size())
        ? p.cfMaterialProps[mat].oy
        : 0.0;
    field.xRe = -g.xRe * ox;
    field.xIm = -g.xIm * ox;
    field.yRe = -g.yRe * oy;
    field.yIm = -g.yIm * oy;
    return true;
  }
  }
  return false;
}
