#define _USE_MATH_DEFINES

#include "SketchTransform.h"

#include "FemmProblem.h"

#include <cmath>

namespace {

using SketchTransform::Motion;

// Which list each ref indexes, per constraint type. The authority is the
// comment on FemmConstraint in FemmProblem.h, and the switch in
// FemmProblemEdit::remapSketchReferences is the same table -- these must
// agree, and the cost of them not agreeing is a reference silently
// resolved against the wrong list.
enum class RefList {
  None,
  Node,
  Segment,
  Arc,
};

void constraintRefLists(const FemmConstraint& c, RefList out[3])
{
  out[0] = out[1] = out[2] = RefList::None;
  switch (c.type) {
  case ConstraintType::Coincident:
    out[0] = out[1] = RefList::Node;
    break;
  case ConstraintType::Horizontal:
  case ConstraintType::Vertical:
    out[0] = RefList::Segment;
    break;
  case ConstraintType::Parallel:
  case ConstraintType::Perpendicular:
    out[0] = out[1] = RefList::Segment;
    break;
  case ConstraintType::Equal:
    out[0] = out[1] = c.isArcPair ? RefList::Arc : RefList::Segment;
    break;
  case ConstraintType::Tangent:
    out[0] = c.firstIsArc ? RefList::Arc : RefList::Segment;
    out[1] = RefList::Arc;
    break;
  case ConstraintType::Concentric:
    out[0] = out[1] = RefList::Arc;
    break;
  case ConstraintType::Symmetric:
    out[0] = out[1] = RefList::Node;
    out[2] = RefList::Segment;
    break;
  }
}

void dimensionRefLists(const FemmDimension& d, RefList out[3])
{
  out[0] = out[1] = out[2] = RefList::None;
  switch (d.type) {
  case DimensionType::Distance:
  case DimensionType::HorizontalDistance:
  case DimensionType::VerticalDistance:
    out[0] = out[1] = RefList::Node;
    break;
  case DimensionType::Radius:
    out[0] = RefList::Arc;
    break;
  case DimensionType::Angle:
    out[0] = out[1] = out[2] = RefList::Node;
    break;
  case DimensionType::AngleLines:
    out[0] = out[1] = RefList::Segment;
    break;
  }
}

const QHash<int, int>* mapFor(RefList list, const QHash<int, int>& nodeMap,
    const QHash<int, int>& segmentMap, const QHash<int, int>& arcMap)
{
  switch (list) {
  case RefList::Node: return &nodeMap;
  case RefList::Segment: return &segmentMap;
  case RefList::Arc: return &arcMap;
  case RefList::None: return nullptr;
  }
  return nullptr;
}

// Normalised to [0, 360).
double norm360(double deg)
{
  double d = std::fmod(deg, 360.0);
  if (d < 0)
    d += 360.0;
  return d;
}

bool nearMultipleOf(double deg, double step, double tol = 1e-9)
{
  const double r = std::fmod(std::fabs(deg), step);
  return r < tol || std::fabs(r - step) < tol;
}

// How a motion maps an axis-aligned constraint.
//
//   Keep    -- Horizontal stays Horizontal
//   Swap    -- Horizontal becomes Vertical and vice versa
//   Neither -- the line ends up at some other angle entirely
enum class AxisFate {
  Keep,
  Swap,
  Neither,
};

AxisFate axisFate(const Motion& m)
{
  switch (m.type) {
  case Motion::Translate:
  case Motion::Scale:
    // A uniform scale about a point does not rotate anything.
    return AxisFate::Keep;
  case Motion::Rotate: {
    const double a = norm360(m.angleDeg);
    if (nearMultipleOf(a, 180.0))
      return AxisFate::Keep;
    if (nearMultipleOf(a - 90.0, 180.0))
      return AxisFate::Swap;
    return AxisFate::Neither;
  }
  case Motion::Mirror: {
    // A line at angle `a` mirrored across an axis at `f` comes out at
    // 2f - a. Horizontal (a = 0) therefore lands at 2f.
    const double twoF = norm360(2.0 * m.angleDeg);
    if (nearMultipleOf(twoF, 180.0))
      return AxisFate::Keep;
    if (nearMultipleOf(twoF - 90.0, 180.0))
      return AxisFate::Swap;
    return AxisFate::Neither;
  }
  }
  return AxisFate::Neither;
}

const char* motionName(const Motion& m)
{
  switch (m.type) {
  case Motion::Translate: return "translation";
  case Motion::Rotate: return "rotation";
  case Motion::Mirror: return "mirror";
  case Motion::Scale: return "scale";
  }
  return "transform";
}

ConstraintType swapAxis(ConstraintType t)
{
  if (t == ConstraintType::Horizontal)
    return ConstraintType::Vertical;
  if (t == ConstraintType::Vertical)
    return ConstraintType::Horizontal;
  return t;
}

DimensionType swapAxis(DimensionType t)
{
  if (t == DimensionType::HorizontalDistance)
    return DimensionType::VerticalDistance;
  if (t == DimensionType::VerticalDistance)
    return DimensionType::HorizontalDistance;
  return t;
}

bool isAxisConstraint(ConstraintType t)
{
  return t == ConstraintType::Horizontal || t == ConstraintType::Vertical;
}

bool isAxisDimension(DimensionType t)
{
  return t == DimensionType::HorizontalDistance || t == DimensionType::VerticalDistance;
}

bool isLengthDimension(DimensionType t)
{
  return t == DimensionType::Distance || t == DimensionType::HorizontalDistance
      || t == DimensionType::VerticalDistance || t == DimensionType::Radius;
}

} // namespace

QString SketchTransform::Report::summary() const
{
  QStringList parts;
  if (constraintsCopied || dimensionsCopied) {
    parts << QStringLiteral("carried %1 constraint(s) and %2 dimension(s) onto the copies")
                 .arg(constraintsCopied).arg(dimensionsCopied);
  }
  if (dimensionsAdjusted)
    parts << QStringLiteral("rescaled %1 dimension value(s)").arg(dimensionsAdjusted);
  if (constraintsDropped)
    parts << QStringLiteral("%1 could not be carried over").arg(constraintsDropped);
  if (conflictsIntroduced) {
    parts << QStringLiteral("%1 now conflict(s) with where the geometry ended up")
                 .arg(conflictsIntroduced);
  }
  if (parts.isEmpty())
    return QString();
  return parts.join(QStringLiteral("; ")) + QStringLiteral(".");
}

void SketchTransform::copyOntoCopies(FemmProblem& p, const QHash<int, int>& nodeMap,
    const QHash<int, int>& segmentMap, const QHash<int, int>& arcMap,
    const Motion& motion, Report* report)
{
  const AxisFate fate = axisFate(motion);

  // Snapshot the counts first: the loops below append, and iterating a
  // list while growing it would copy the copies.
  const int constraintCount = p.constraints.size();
  const int dimensionCount = p.dimensions.size();

  for (int i = 0; i < constraintCount; i++) {
    const FemmConstraint& c = p.constraints[i];
    RefList lists[3];
    constraintRefLists(c, lists);
    const int refs[3] = { c.refA, c.refB, c.refC };

    FemmConstraint copy = c;
    int* out[3] = { &copy.refA, &copy.refB, &copy.refC };
    bool allCopied = true;
    for (int k = 0; k < 3; k++) {
      const QHash<int, int>* m = mapFor(lists[k], nodeMap, segmentMap, arcMap);
      if (!m)
        continue; // this slot is unused for this type
      const auto it = m->constFind(refs[k]);
      if (it == m->constEnd()) {
        allCopied = false;
        break;
      }
      *out[k] = it.value();
    }
    // A constraint with one end outside the selection would, copied,
    // tie the copy back to the original. That is a relationship nobody
    // asked for, and the solver would enforce it.
    if (!allCopied)
      continue;

    if (isAxisConstraint(c.type)) {
      if (fate == AxisFate::Neither) {
        if (report) {
          report->constraintsDropped++;
          report->notes << QStringLiteral(
              "a %1 constraint was not carried onto the copy: after this %2 the line "
              "is neither horizontal nor vertical")
                               .arg(c.type == ConstraintType::Horizontal
                                       ? QStringLiteral("Horizontal")
                                       : QStringLiteral("Vertical"))
                               .arg(QString::fromLatin1(motionName(motion)));
        }
        continue;
      }
      if (fate == AxisFate::Swap)
        copy.type = swapAxis(c.type);
    }

    p.constraints.push_back(copy);
    if (report)
      report->constraintsCopied++;
  }

  for (int i = 0; i < dimensionCount; i++) {
    const FemmDimension& d = p.dimensions[i];
    RefList lists[3];
    dimensionRefLists(d, lists);
    const int refs[3] = { d.refA, d.refB, d.refC };

    FemmDimension copy = d;
    int* out[3] = { &copy.refA, &copy.refB, &copy.refC };
    bool allCopied = true;
    for (int k = 0; k < 3; k++) {
      const QHash<int, int>* m = mapFor(lists[k], nodeMap, segmentMap, arcMap);
      if (!m)
        continue;
      const auto it = m->constFind(refs[k]);
      if (it == m->constEnd()) {
        allCopied = false;
        break;
      }
      *out[k] = it.value();
    }
    if (!allCopied)
      continue;

    if (isAxisDimension(d.type)) {
      if (fate == AxisFate::Neither) {
        if (report) {
          report->constraintsDropped++;
          report->notes << QStringLiteral(
              "an axis-aligned distance dimension was not carried onto the copy: "
              "after this %1 it measures along neither axis")
                               .arg(QString::fromLatin1(motionName(motion)));
        }
        continue;
      }
      if (fate == AxisFate::Swap)
        copy.type = swapAxis(d.type);
    }

    if (motion.type == Motion::Scale && isLengthDimension(d.type))
      copy.value = d.value * std::fabs(motion.factor);

    p.dimensions.push_back(copy);
    if (report)
      report->dimensionsCopied++;
  }
}

void SketchTransform::adjustAfterInPlace(FemmProblem& p, const Motion& motion, Report* report)
{
  // "Was this entity part of what moved?" -- asked of the entity itself
  // rather than of a map, since an in-place transform builds none.
  auto nodeMoved = [&](int i) {
    return i >= 0 && i < p.nodes.size() && p.nodes[i].isSelected;
  };
  auto segmentMoved = [&](int i) {
    return i >= 0 && i < p.segments.size()
        && nodeMoved(p.segments[i].n0) && nodeMoved(p.segments[i].n1);
  };
  auto arcMoved = [&](int i) {
    return i >= 0 && i < p.arcSegments.size()
        && nodeMoved(p.arcSegments[i].n0) && nodeMoved(p.arcSegments[i].n1);
  };
  auto moved = [&](RefList list, int i) {
    switch (list) {
    case RefList::Node: return nodeMoved(i);
    case RefList::Segment: return segmentMoved(i);
    case RefList::Arc: return arcMoved(i);
    case RefList::None: return true; // unused slot: no obstacle
    }
    return false;
  };

  const AxisFate fate = axisFate(motion);

  for (FemmConstraint& c : p.constraints) {
    if (!isAxisConstraint(c.type))
      continue; // every other kind is preserved by a rigid motion or a uniform scale
    RefList lists[3];
    constraintRefLists(c, lists);
    if (!moved(lists[0], c.refA))
      continue; // it constrains something that did not move

    if (fate == AxisFate::Swap) {
      // The line really is the other axis now, so the constraint is
      // rewritten rather than left to fight the geometry.
      c.type = swapAxis(c.type);
    } else if (fate == AxisFate::Neither && report) {
      // Deliberately NOT dropped. The constraint still points at the
      // right entity and still says what the user meant; it just
      // disagrees with where the geometry now is, and the next solve
      // will pull it back. Saying so is right; deciding for them is not.
      report->conflictsIntroduced++;
      report->notes << QStringLiteral(
          "a %1 constraint now conflicts with the rotated geometry and will pull it "
          "back on the next solve -- remove it first if the new orientation is what "
          "you want")
                           .arg(c.type == ConstraintType::Horizontal
                                   ? QStringLiteral("Horizontal")
                                   : QStringLiteral("Vertical"));
    }
  }

  for (FemmDimension& d : p.dimensions) {
    RefList lists[3];
    dimensionRefLists(d, lists);
    bool allMoved = true;
    const int refs[3] = { d.refA, d.refB, d.refC };
    for (int k = 0; k < 3; k++) {
      if (lists[k] != RefList::None && !moved(lists[k], refs[k]))
        allMoved = false;
    }

    if (motion.type == Motion::Scale && isLengthDimension(d.type)) {
      if (allMoved) {
        // The geometry is a different size now, so the dimension that
        // measures it has to say so. Without this the next solve drags
        // the geometry back to the old size and the scale is undone --
        // silently, since nothing reports it.
        d.value *= std::fabs(motion.factor);
        if (report)
          report->dimensionsAdjusted++;
      } else if (report) {
        // Half inside the scale and half outside. There is no correct
        // new value: scaling it would misdescribe the unscaled end, and
        // leaving it misdescribes the scaled one.
        report->conflictsIntroduced++;
        report->notes << QStringLiteral(
            "a length dimension spans both scaled and unscaled geometry, so its value "
            "was left alone and now measures neither correctly");
      }
    }

    if (isAxisDimension(d.type) && allMoved) {
      if (fate == AxisFate::Swap)
        d.type = swapAxis(d.type);
      else if (fate == AxisFate::Neither && report)
        report->conflictsIntroduced++;
    }
  }
}
