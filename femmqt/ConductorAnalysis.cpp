#include "ConductorAnalysis.h"

#include "FemmProblem.h"
#include "ProblemKind.h"

#include <algorithm>
#include <cmath>

ConductorAnalysis::Result ConductorAnalysis::analyse(const SolvedMesh& mesh,
    const FemmProblem& p, int conductor)
{
  Result r;
  if (conductor <= 0)
    return r;

  const int index = conductor - 1;
  if (index >= 0 && index < p.conductorProps.size())
    r.name = p.conductorProps[index].name;

  // --- the equipotential value ---------------------------------------------
  double sumRe = 0, sumIm = 0;
  double lo = 0, hi = 0;
  bool first = true;
  for (const SolutionNode& n : mesh.nodes) {
    if (n.conductor != conductor)
      continue;
    sumRe += n.potentialRe;
    sumIm += n.potentialIm;
    if (first) {
      lo = hi = n.potentialRe;
      first = false;
    } else {
      lo = std::min(lo, n.potentialRe);
      hi = std::max(hi, n.potentialRe);
    }
    r.nodeCount++;
  }
  if (r.nodeCount == 0)
    return r; // no node carries this tag: not an error, just not present

  r.potentialRe = sumRe / r.nodeCount;
  r.potentialIm = sumIm / r.nodeCount;
  // A conductor is an equipotential by definition, so this should be
  // ~zero. Reporting it lets a caller notice when it is not, instead of
  // quietly averaging two things that are not one conductor.
  r.potentialSpread = hi - lo;

  // --- the flux crossing its surface ---------------------------------------
  //
  // The surface is every element edge whose BOTH endpoints carry the
  // tag. Each such edge is walked with the outward normal -- outward
  // meaning away from the element that owns it, which is what makes the
  // sum the flux leaving the conductor rather than a signed quantity
  // that depends on mesh node ordering.
  const double lc = lengthToMeters(p.lengthUnits);

  for (int i = 0; i < mesh.elements.size(); i++) {
    const SolutionElement& e = mesh.elements[i];
    const int n = mesh.nodes.size();
    if (e.p0 < 0 || e.p0 >= n || e.p1 < 0 || e.p1 >= n || e.p2 < 0 || e.p2 >= n)
      continue;

    SolutionField::Vector2 f;
    if (!SolutionField::elementField(mesh, p, i, f))
      continue;

    const double cx = (mesh.nodes[e.p0].x + mesh.nodes[e.p1].x + mesh.nodes[e.p2].x) / 3.0;
    const double cy = (mesh.nodes[e.p0].y + mesh.nodes[e.p1].y + mesh.nodes[e.p2].y) / 3.0;

    const int pts[3] = { e.p0, e.p1, e.p2 };
    for (int k = 0; k < 3; k++) {
      const int a = pts[k];
      const int b = pts[(k + 1) % 3];
      if (mesh.nodes[a].conductor != conductor || mesh.nodes[b].conductor != conductor)
        continue;

      const double ex = mesh.nodes[b].x - mesh.nodes[a].x;
      const double ey = mesh.nodes[b].y - mesh.nodes[a].y;
      const double len = std::hypot(ex, ey);
      if (len == 0.0)
        continue;

      // Either normal is perpendicular to the edge; pick the one facing
      // away from the element's centroid.
      double nx = ey / len;
      double ny = -ex / len;
      const double mx = (mesh.nodes[a].x + mesh.nodes[b].x) / 2.0;
      const double my = (mesh.nodes[a].y + mesh.nodes[b].y) / 2.0;
      if ((mx - cx) * nx + (my - cy) * ny < 0) {
        nx = -nx;
        ny = -ny;
      }

      const double dl = len * lc;
      r.fluxRe += (f.xRe * nx + f.yRe * ny) * dl;
      r.fluxIm += (f.xIm * nx + f.yIm * ny) * dl;
      r.surfaceLength += dl;
    }
  }

  r.ok = true;
  return r;
}

SolutionField::Quantity ConductorAnalysis::potentialQuantity(FemmProblemKind kind)
{
  switch (kind) {
  case FemmProblemKind::Electrostatics:
  case FemmProblemKind::CurrentFlow:
    return { QStringLiteral("Conductor voltage"), QStringLiteral("V") };
  case FemmProblemKind::HeatFlow:
    return { QStringLiteral("Conductor temperature"), QStringLiteral("K") };
  case FemmProblemKind::Magnetics:
    // Magnetics has circuits, not conductors -- see CircuitAnalysis.
    return {};
  }
  return {};
}

SolutionField::Quantity ConductorAnalysis::fluxQuantity(FemmProblemKind kind)
{
  switch (kind) {
  case FemmProblemKind::Electrostatics:
    return { QStringLiteral("Total charge"), QStringLiteral("C/m") };
  case FemmProblemKind::HeatFlow:
    return { QStringLiteral("Heat flow out"), QStringLiteral("W/m") };
  case FemmProblemKind::CurrentFlow:
    return { QStringLiteral("Total current"), QStringLiteral("A/m") };
  case FemmProblemKind::Magnetics:
    return {};
  }
  return {};
}
