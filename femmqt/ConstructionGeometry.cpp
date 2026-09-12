#define _USE_MATH_DEFINES

#include "ConstructionGeometry.h"

#include "FemmProblem.h"
#include "FemmProblemEdit.h"

#include <cmath>

namespace {

ConstructionGeometry::Result fail(const QString& why)
{
  ConstructionGeometry::Result r;
  r.ok = false;
  r.message = why;
  return r;
}

} // namespace

ConstructionGeometry::Result ConstructionGeometry::setSelectedConstruction(
    FemmProblem& p, bool toConstruction)
{
  Result r;
  for (FemmSegment& s : p.segments) {
    if (s.isSelected && s.isConstruction != toConstruction) {
      s.isConstruction = toConstruction;
      r.entitiesChanged++;
    }
  }
  for (FemmArcSegment& a : p.arcSegments) {
    if (a.isSelected && a.isConstruction != toConstruction) {
      a.isConstruction = toConstruction;
      r.entitiesChanged++;
    }
  }
  // Nodes follow only when selected in their own right. A node shared
  // with real geometry that got dragged along would be written to the
  // .fem anyway (withoutConstruction keeps any node a surviving edge
  // needs), so flagging it would be a lie the file does not tell.
  for (FemmNode& n : p.nodes) {
    if (n.isSelected && n.isConstruction != toConstruction) {
      n.isConstruction = toConstruction;
      r.entitiesChanged++;
    }
  }

  if (r.entitiesChanged == 0) {
    return fail(toConstruction
            ? QStringLiteral("Nothing selected is real geometry, so there is nothing "
                             "to convert to construction.")
            : QStringLiteral("Nothing selected is construction geometry, so there is "
                             "nothing to convert back."));
  }

  r.ok = true;
  r.message = toConstruction
      ? QStringLiteral("%1 entit(ies) are now construction geometry: they will not be "
                       "meshed or solved, and are not written to the .fem.")
            .arg(r.entitiesChanged)
      : QStringLiteral("%1 entit(ies) are now real geometry and will be meshed and "
                       "solved.")
            .arg(r.entitiesChanged);
  return r;
}

ConstructionGeometry::Result ConstructionGeometry::addCentreline(
    FemmProblem& p, double x0, double y0, double x1, double y1)
{
  if (std::hypot(x1 - x0, y1 - y0) <= 0)
    return fail(QStringLiteral("A centreline needs two different points."));

  const int a = FemmProblemEdit::addNode(p, x0, y0);
  const int b = FemmProblemEdit::addNode(p, x1, y1);
  p.nodes[a].isConstruction = true;
  p.nodes[b].isConstruction = true;
  const int seg = FemmProblemEdit::addSegment(p, a, b);
  p.segments[seg].isConstruction = true;

  Result r;
  r.ok = true;
  r.entitiesChanged = 1;
  r.message = QStringLiteral("Centreline added as construction geometry.");
  return r;
}

ConstructionGeometry::Result ConstructionGeometry::addBoltCircle(
    FemmProblem& p, double cx, double cy, double radius, int count, double startAngleDeg)
{
  if (radius <= 0)
    return fail(QStringLiteral("A bolt circle needs a radius greater than zero."));
  if (count < 1)
    return fail(QStringLiteral("A bolt circle needs at least one position."));

  // The circle itself, as two semicircular construction arcs. Two rather
  // than one because a FEMM arc is defined by two endpoints and an
  // included angle, so a full 360-degree arc has no chord to derive its
  // centre from -- the same reason the Draw Circle tool builds a circle
  // this way.
  const int left = FemmProblemEdit::addNode(p, cx - radius, cy);
  const int right = FemmProblemEdit::addNode(p, cx + radius, cy);
  p.nodes[left].isConstruction = true;
  p.nodes[right].isConstruction = true;
  const int top = FemmProblemEdit::addArcSegment(p, right, left, 180.0, 5.0);
  const int bottom = FemmProblemEdit::addArcSegment(p, left, right, 180.0, 5.0);
  p.arcSegments[top].isConstruction = true;
  p.arcSegments[bottom].isConstruction = true;

  Result r;
  r.entitiesChanged = 2;
  for (int i = 0; i < count; i++) {
    const double th = (startAngleDeg + 360.0 * i / count) * M_PI / 180.0;
    const int n = FemmProblemEdit::addNode(p,
        cx + radius * std::cos(th), cy + radius * std::sin(th));
    p.nodes[n].isConstruction = true;
    r.entitiesChanged++;
  }

  r.ok = true;
  r.message = QStringLiteral("Bolt circle added: %1 construction position(s) on a "
                             "construction circle of radius %2.")
                  .arg(count).arg(radius, 0, 'g', 6);
  return r;
}

ConstructionGeometry::Result ConstructionGeometry::addReferenceRectangle(
    FemmProblem& p, double x0, double y0, double x1, double y1)
{
  if (x0 == x1 || y0 == y1)
    return fail(QStringLiteral("A reference rectangle needs two opposite corners, not "
                               "two points on the same line."));

  const double lo_x = std::min(x0, x1), hi_x = std::max(x0, x1);
  const double lo_y = std::min(y0, y1), hi_y = std::max(y0, y1);

  const int a = FemmProblemEdit::addNode(p, lo_x, lo_y);
  const int b = FemmProblemEdit::addNode(p, hi_x, lo_y);
  const int c = FemmProblemEdit::addNode(p, hi_x, hi_y);
  const int d = FemmProblemEdit::addNode(p, lo_x, hi_y);
  for (int n : { a, b, c, d })
    p.nodes[n].isConstruction = true;

  const int sides[4] = {
    FemmProblemEdit::addSegment(p, a, b),
    FemmProblemEdit::addSegment(p, b, c),
    FemmProblemEdit::addSegment(p, c, d),
    FemmProblemEdit::addSegment(p, d, a),
  };
  for (int s : sides)
    p.segments[s].isConstruction = true;

  Result r;
  r.ok = true;
  r.entitiesChanged = 4;
  r.message = QStringLiteral("Reference rectangle added as construction geometry.");
  return r;
}
