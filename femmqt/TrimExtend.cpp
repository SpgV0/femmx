#define _USE_MATH_DEFINES

#include "TrimExtend.h"

#include "FemmProblem.h"
#include "FemmProblemEdit.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

namespace {

using Complex = std::complex<double>;
using TrimExtend::EntityKind;

// Tolerances are RELATIVE to the entity being worked on, never absolute.
// A magnetics model may be drawn in millimetres or in metres, and an
// absolute epsilon that is generous in one is meaningless in the other.
constexpr double kOnCurveRel = 1e-6;  // "this point lies on that curve"
constexpr double kParamRel = 1e-9;    // "this parameter is strictly inside"

// One uniform view of a segment or an arc, so trim/extend/split are
// written once instead of twice. The arc form carries the same
// convention as the rest of the editor: t=0 at n0, sweeping
// counterclockwise through `sweepDeg` degrees to t=1 at n1.
struct Curve {
  bool isArc = false;
  int n0 = -1, n1 = -1;

  Complex a, b;   // segment endpoints
  Complex c;      // arc centre
  double R = 0;   // arc radius
  double th0 = 0; // arc start angle, radians
  double sweepDeg = 0;

  Complex at(double t) const
  {
    if (!isArc)
      return a + t * (b - a);
    const double th = th0 + t * sweepDeg * M_PI / 180.0;
    return c + R * Complex(std::cos(th), std::sin(th));
  }

  // Arc length of the whole entity -- the scale the relative tolerances
  // above are taken against.
  double length() const
  {
    return isArc ? R * std::abs(sweepDeg) * M_PI / 180.0 : std::abs(b - a);
  }

  // Parameter of the full circle, i.e. where an arc's extension would
  // bite its own tail. 1.0 for a segment, which has no such limit.
  double tFull() const
  {
    return isArc && std::abs(sweepDeg) > 0 ? 360.0 / sweepDeg : 1.0;
  }

  // Parameter of `pt`, NOT clamped. For a segment this is the position
  // along the infinite line; for an arc it is the position around the
  // whole circle measured forward from n0, so it lands in [0, tFull).
  double paramOf(Complex pt) const
  {
    if (!isArc) {
      const Complex d = b - a;
      const double len2 = std::norm(d);
      if (len2 <= 0)
        return 0;
      return ((pt - a).real() * d.real() + (pt - a).imag() * d.imag()) / len2;
    }
    if (std::abs(sweepDeg) <= 0)
      return 0;
    double zDeg = std::arg((pt - c) * std::polar(1.0, -th0)) * 180.0 / M_PI;
    if (zDeg < 0)
      zDeg += 360.0;
    return zDeg / sweepDeg;
  }
};

bool buildCurve(const FemmProblem& p, EntityKind kind, int index, Curve& out)
{
  const int nn = p.nodes.size();
  if (kind == EntityKind::Segment) {
    if (index < 0 || index >= p.segments.size())
      return false;
    const FemmSegment& s = p.segments[index];
    if (s.n0 < 0 || s.n0 >= nn || s.n1 < 0 || s.n1 >= nn)
      return false;
    out = Curve();
    out.isArc = false;
    out.n0 = s.n0;
    out.n1 = s.n1;
    out.a = Complex(p.nodes[s.n0].x, p.nodes[s.n0].y);
    out.b = Complex(p.nodes[s.n1].x, p.nodes[s.n1].y);
    return std::abs(out.b - out.a) > 0;
  }

  if (index < 0 || index >= p.arcSegments.size())
    return false;
  const FemmArcSegment& arc = p.arcSegments[index];
  if (arc.n0 < 0 || arc.n0 >= nn || arc.n1 < 0 || arc.n1 >= nn)
    return false;
  Complex centre;
  double R = 0;
  if (!FemmProblemEdit::circleFromArc(p, arc, centre, R))
    return false;
  out = Curve();
  out.isArc = true;
  out.n0 = arc.n0;
  out.n1 = arc.n1;
  out.c = centre;
  out.R = R;
  out.a = Complex(p.nodes[arc.n0].x, p.nodes[arc.n0].y);
  out.b = Complex(p.nodes[arc.n1].x, p.nodes[arc.n1].y);
  out.th0 = std::arg(out.a - centre);
  out.sweepDeg = arc.arcLength;
  return R > 0 && std::abs(arc.arcLength) > 0;
}

// Is `t` within the curve's own extent, allowing the endpoints? Used for
// the OTHER curve in an intersection: an entity whose endpoint lands on
// the one being trimmed is a cutting edge, and the commonest one there
// is, since that is exactly what endpoint snapping produces.
//
// An arc's paramOf wraps to [0, tFull), so a point a rounding error
// BEFORE its start reads as very nearly tFull rather than very slightly
// negative -- hence the second window.
bool withinInclusive(const Curve& cv, double t)
{
  if (t >= -kOnCurveRel && t <= 1.0 + kOnCurveRel)
    return true;
  return cv.isArc && t >= cv.tFull() - kOnCurveRel;
}

// Every point where the two curves meet. `clampA` false treats A as the
// infinite line / full circle it lies on, which is what extend needs.
QVector<Complex> meetingPoints(const Curve& A, const Curve& B, bool clampA)
{
  QVector<Complex> pts;
  const double scale = std::max(A.length(), B.length());
  if (scale <= 0)
    return pts;
  const double tolDist = kOnCurveRel * scale;

  auto acceptA = [&](Complex pt) { return !clampA || withinInclusive(A, A.paramOf(pt)); };
  auto acceptB = [&](Complex pt) { return withinInclusive(B, B.paramOf(pt)); };

  if (!A.isArc && !B.isArc) {
    const Complex r = A.b - A.a, s = B.b - B.a;
    const double denom = r.real() * s.imag() - r.imag() * s.real();
    const double lenA = std::abs(r), lenB = std::abs(s);
    if (lenA <= 0 || lenB <= 0)
      return pts;
    // Parallel (or collinear). A collinear overlap has no single
    // crossing point, and inventing one would be a guess.
    if (std::abs(denom) < 1e-12 * lenA * lenB)
      return pts;
    const Complex d = B.a - A.a;
    const double t = (d.real() * s.imag() - d.imag() * s.real()) / denom;
    const Complex pt = A.a + t * r;
    if (acceptA(pt) && acceptB(pt))
      pts.push_back(pt);
    return pts;
  }

  if (A.isArc && B.isArc) {
    const double d = std::abs(B.c - A.c);
    if (d <= 0)
      return pts; // concentric: either no meeting or the same circle
    if (d > A.R + B.R + tolDist || d < std::abs(A.R - B.R) - tolDist)
      return pts;
    const double aa = (d * d + A.R * A.R - B.R * B.R) / (2 * d);
    const double h2 = A.R * A.R - aa * aa;
    const double h = h2 > 0 ? std::sqrt(h2) : 0.0;
    const Complex u = (B.c - A.c) / d;
    const Complex mid = A.c + aa * u;
    const Complex perp = Complex(-u.imag(), u.real()) * h;
    for (Complex pt : { mid + perp, mid - perp }) {
      if (acceptA(pt) && acceptB(pt))
        pts.push_back(pt);
      if (h <= 0)
        break; // tangent: one point, not two identical ones
    }
    return pts;
  }

  // One of each. Solve against the arc's circle with the segment's line.
  const Curve& line = A.isArc ? B : A;
  const Curve& circ = A.isArc ? A : B;
  const Complex dir = line.b - line.a;
  const double len = std::abs(dir);
  if (len <= 0 || circ.R <= 0)
    return pts;
  const Complex f = line.a - circ.c;
  const double qa = std::norm(dir);
  const double qb = 2 * (f.real() * dir.real() + f.imag() * dir.imag());
  const double qc = std::norm(f) - circ.R * circ.R;
  double disc = qb * qb - 4 * qa * qc;
  if (disc < 0) {
    // A near-tangent line can miss by a rounding error; treat anything
    // inside the distance tolerance as the tangency it really is.
    if (-disc > 4 * qa * tolDist * tolDist * 4)
      return pts;
    disc = 0;
  }
  const double sq = std::sqrt(disc);
  for (double t : { (-qb - sq) / (2 * qa), (-qb + sq) / (2 * qa) }) {
    const Complex pt = line.a + t * dir;
    if (acceptA(pt) && acceptB(pt))
      pts.push_back(pt);
    if (sq <= 0)
      break;
  }
  return pts;
}

// Distance from `pt` to the nearest point actually ON the curve.
double distanceToCurve(const Curve& cv, Complex pt)
{
  if (!cv.isArc) {
    const double t = std::clamp(cv.paramOf(pt), 0.0, 1.0);
    return std::abs(pt - cv.at(t));
  }
  const double t = cv.paramOf(pt);
  if (t >= 0 && t <= 1.0)
    return std::abs(std::abs(pt - cv.c) - cv.R);
  return std::min(std::abs(pt - cv.a), std::abs(pt - cv.b));
}

void appendUnique(QVector<double>& v, double t, double eps)
{
  for (double existing : v) {
    if (std::abs(existing - t) <= eps)
      return;
  }
  v.push_back(t);
}

// The node at `pt`, reusing an existing one when there is one there.
// Inventing a second node on top of an existing one is what produces
// geometry that looks joined and does not mesh.
int nodeAtPoint(FemmProblem& p, Complex pt, double tolDist)
{
  for (int i = 0; i < p.nodes.size(); i++) {
    if (std::hypot(p.nodes[i].x - pt.real(), p.nodes[i].y - pt.imag()) <= tolDist)
      return i;
  }
  return FemmProblemEdit::addNode(p, pt.real(), pt.imag());
}

// How many constraints and dimensions name this entity. They stay valid
// -- the entity keeps its index -- but one that measured the whole thing
// now measures whatever survived, and the user has to be told.
int countReferences(const FemmProblem& p, EntityKind kind, int index)
{
  const bool arc = kind == EntityKind::Arc;
  int n = 0;
  for (const FemmConstraint& c : p.constraints) {
    switch (c.type) {
    case ConstraintType::Horizontal:
    case ConstraintType::Vertical:
      if (!arc && c.refA == index)
        n++;
      break;
    case ConstraintType::Parallel:
    case ConstraintType::Perpendicular:
      if (!arc && (c.refA == index || c.refB == index))
        n++;
      break;
    case ConstraintType::Equal:
      if (c.isArcPair == arc && (c.refA == index || c.refB == index))
        n++;
      break;
    case ConstraintType::Tangent:
      if ((c.firstIsArc == arc && c.refA == index) || (arc && c.refB == index))
        n++;
      break;
    case ConstraintType::Concentric:
      if (arc && (c.refA == index || c.refB == index))
        n++;
      break;
    case ConstraintType::Symmetric:
      if (!arc && c.refC == index)
        n++;
      break;
    case ConstraintType::Coincident:
      break;
    }
  }
  // Which list each dimension's refs index is documented on
  // DimensionType in FemmProblem.h; the authoritative switch is
  // FemmProblemEdit::remapSketchReferences, which this mirrors. Note
  // that Angle references NODES (vertex plus two ray ends) while
  // AngleLines references SEGMENTS -- conflating the two is exactly the
  // silent-alias mistake, since both are "an angle dimension".
  for (const FemmDimension& d : p.dimensions) {
    switch (d.type) {
    case DimensionType::Radius:
      if (arc && d.refA == index)
        n++;
      break;
    case DimensionType::AngleLines:
      if (!arc && (d.refA == index || d.refB == index))
        n++;
      break;
    case DimensionType::Distance:
    case DimensionType::HorizontalDistance:
    case DimensionType::VerticalDistance:
    case DimensionType::Angle:
      break; // node references; unaffected by an entity's index
    }
  }
  return n;
}

TrimExtend::Result failure(const QString& why)
{
  TrimExtend::Result r;
  r.ok = false;
  r.message = why;
  return r;
}

} // namespace

// ---------------------------------------------------------------------------

QVector<double> TrimExtend::cutParameters(const FemmProblem& p, EntityKind kind, int index)
{
  QVector<double> cuts;
  Curve A;
  if (!buildCurve(p, kind, index, A))
    return cuts;
  const double lenA = A.length();
  if (lenA <= 0)
    return cuts;
  const double tolDist = kOnCurveRel * lenA;
  const double tolParam = std::max(kParamRel, tolDist / lenA);

  auto consider = [&](Complex pt) {
    const double t = A.paramOf(pt);
    if (t > tolParam && t < 1.0 - tolParam)
      appendUnique(cuts, t, tolParam);
  };

  for (int j = 0; j < p.segments.size(); j++) {
    if (kind == EntityKind::Segment && j == index)
      continue;
    Curve B;
    if (!buildCurve(p, EntityKind::Segment, j, B))
      continue;
    for (Complex pt : meetingPoints(A, B, true))
      consider(pt);
  }
  for (int j = 0; j < p.arcSegments.size(); j++) {
    if (kind == EntityKind::Arc && j == index)
      continue;
    Curve B;
    if (!buildCurve(p, EntityKind::Arc, j, B))
      continue;
    for (Complex pt : meetingPoints(A, B, true))
      consider(pt);
  }

  // A bare node sitting on the entity is a cut too. Nothing crosses
  // there, so the scans above cannot see it, but the user put it there
  // and expects to be able to trim to it.
  for (int i = 0; i < p.nodes.size(); i++) {
    if (i == A.n0 || i == A.n1)
      continue;
    const Complex pt(p.nodes[i].x, p.nodes[i].y);
    if (distanceToCurve(A, pt) <= tolDist)
      consider(pt);
  }

  std::sort(cuts.begin(), cuts.end());
  return cuts;
}

// ---------------------------------------------------------------------------

TrimExtend::Result TrimExtend::split(FemmProblem& p, EntityKind kind, int index, double x, double y)
{
  Curve A;
  if (!buildCurve(p, kind, index, A))
    return failure(QStringLiteral("That entity cannot be split -- it is degenerate "
                                  "(zero length, or a zero-angle arc)."));
  const double lenA = A.length();
  const double tolDist = kOnCurveRel * lenA;
  const double tolParam = std::max(kParamRel, tolDist / lenA);

  const double t = A.paramOf(Complex(x, y));
  if (t <= tolParam || t >= 1.0 - tolParam)
    return failure(QStringLiteral("Pick a point along the entity, away from its ends "
                                  "-- splitting at an end would produce a zero-length "
                                  "piece rather than two."));

  Result r;
  r.affectedReferences = countReferences(p, kind, index);
  const Complex pt = A.at(t);
  r.nodeA = nodeAtPoint(p, pt, tolDist);

  if (kind == EntityKind::Segment) {
    FemmSegment tail = p.segments[index]; // copy first: push_back may reallocate
    const int origEnd = tail.n1;
    p.segments[index].n1 = r.nodeA;
    tail.n0 = r.nodeA;
    tail.n1 = origEnd;
    p.segments.push_back(tail);
  } else {
    FemmArcSegment tail = p.arcSegments[index];
    const int origEnd = tail.n1;
    const double sweep = tail.arcLength;
    p.arcSegments[index].n1 = r.nodeA;
    p.arcSegments[index].arcLength = t * sweep;
    tail.n0 = r.nodeA;
    tail.n1 = origEnd;
    tail.arcLength = (1.0 - t) * sweep;
    p.arcSegments.push_back(tail);
  }

  r.ok = true;
  if (r.affectedReferences > 0) {
    r.message = QStringLiteral("Split. %1 constraint(s)/dimension(s) referred to this "
                               "entity and now refer to the first half only.")
                    .arg(r.affectedReferences);
  }
  return r;
}

// ---------------------------------------------------------------------------

TrimExtend::Result TrimExtend::trim(FemmProblem& p, EntityKind kind, int index, double x, double y)
{
  Curve A;
  if (!buildCurve(p, kind, index, A))
    return failure(QStringLiteral("That entity cannot be trimmed -- it is degenerate "
                                  "(zero length, or a zero-angle arc)."));
  const double lenA = A.length();
  const double tolDist = kOnCurveRel * lenA;
  const double tolParam = std::max(kParamRel, tolDist / lenA);

  const double tPick = std::clamp(A.paramOf(Complex(x, y)), 0.0, 1.0);
  const QVector<double> cuts = cutParameters(p, kind, index);

  // The piece under the cursor runs between the nearest cut below and
  // the nearest above. A cut landing ON the pick is ambiguous -- the user
  // clicked a junction -- and is ignored, which widens the piece rather
  // than picking one of the two arbitrarily.
  double lo = 0.0, hi = 1.0;
  for (double t : cuts) {
    if (t < tPick - tolParam)
      lo = std::max(lo, t);
    else if (t > tPick + tolParam)
      hi = std::min(hi, t);
  }

  const bool keepLow = lo > tolParam;
  const bool keepHigh = hi < 1.0 - tolParam;

  Result r;
  r.affectedReferences = countReferences(p, kind, index);

  if (!keepLow && !keepHigh) {
    // Nothing bounds the picked piece, so the piece is the whole entity.
    if (kind == EntityKind::Segment)
      FemmProblemEdit::deleteSegment(p, index);
    else
      FemmProblemEdit::deleteArcSegment(p, index);
    r.ok = true;
    r.message = cuts.isEmpty()
        ? QStringLiteral("Nothing crosses that entity, so the whole of it was removed.")
        : QStringLiteral("The picked piece spans the whole entity, so all of it was removed.");
    // deleteSegment/deleteArcSegment already drop references that can no
    // longer resolve (see FemmProblemEdit::remapSketchReferences), so
    // reporting them as merely "affected" here would understate it.
    if (r.affectedReferences > 0) {
      r.message += QStringLiteral(" %1 constraint(s)/dimension(s) that referred to it "
                                  "were removed with it.")
                       .arg(r.affectedReferences);
    }
    return r;
  }

  const int origEnd = (kind == EntityKind::Segment) ? p.segments[index].n1 : p.arcSegments[index].n1;
  const double sweep = (kind == EntityKind::Arc) ? p.arcSegments[index].arcLength : 0.0;

  if (keepLow)
    r.nodeA = nodeAtPoint(p, A.at(lo), tolDist);
  if (keepHigh)
    r.nodeB = nodeAtPoint(p, A.at(hi), tolDist);

  if (kind == EntityKind::Segment) {
    if (keepLow && keepHigh) {
      FemmSegment tail = p.segments[index];
      p.segments[index].n1 = r.nodeA;
      tail.n0 = r.nodeB;
      tail.n1 = origEnd;
      p.segments.push_back(tail);
    } else if (keepLow) {
      p.segments[index].n1 = r.nodeA;
    } else {
      p.segments[index].n0 = r.nodeB;
    }
  } else {
    if (keepLow && keepHigh) {
      FemmArcSegment tail = p.arcSegments[index];
      p.arcSegments[index].n1 = r.nodeA;
      p.arcSegments[index].arcLength = lo * sweep;
      tail.n0 = r.nodeB;
      tail.n1 = origEnd;
      tail.arcLength = (1.0 - hi) * sweep;
      p.arcSegments.push_back(tail);
    } else if (keepLow) {
      p.arcSegments[index].n1 = r.nodeA;
      p.arcSegments[index].arcLength = lo * sweep;
    } else {
      p.arcSegments[index].n0 = r.nodeB;
      p.arcSegments[index].arcLength = (1.0 - hi) * sweep;
    }
  }

  r.ok = true;
  if (r.affectedReferences > 0) {
    r.message = QStringLiteral("Trimmed. %1 constraint(s)/dimension(s) still refer to "
                               "this entity, which is now shorter than when they were "
                               "created.")
                    .arg(r.affectedReferences);
  }
  return r;
}

// ---------------------------------------------------------------------------

TrimExtend::Result TrimExtend::extend(FemmProblem& p, EntityKind kind, int index, double x, double y)
{
  Curve A;
  if (!buildCurve(p, kind, index, A))
    return failure(QStringLiteral("That entity cannot be extended -- it is degenerate "
                                  "(zero length, or a zero-angle arc)."));
  const double lenA = A.length();
  const double tolDist = kOnCurveRel * lenA;
  const double tolParam = std::max(kParamRel, tolDist / lenA);

  // Which end to grow, by plain distance to the two endpoints rather
  // than by parameter. An arc's parameter wraps, so "past the n1 end"
  // and "past the n0 end" are both values above 1 and comparing them
  // against 0.5 gets it backwards for half the clicks.
  const Complex pick(x, y);
  const bool forward = std::abs(pick - A.b) < std::abs(pick - A.a);
  const int endNode = forward ? A.n1 : A.n0;

  // Moving a shared node would drag everything else attached to it;
  // re-pointing this entity at a fresh node would tear the junction open
  // and leave a gap that meshes as one region instead of two. Both are
  // worse than saying no.
  int sharers = 0;
  for (int j = 0; j < p.segments.size(); j++) {
    if (kind == EntityKind::Segment && j == index)
      continue;
    if (p.segments[j].n0 == endNode || p.segments[j].n1 == endNode)
      sharers++;
  }
  for (int j = 0; j < p.arcSegments.size(); j++) {
    if (kind == EntityKind::Arc && j == index)
      continue;
    if (p.arcSegments[j].n0 == endNode || p.arcSegments[j].n1 == endNode)
      sharers++;
  }
  if (sharers > 0) {
    return failure(QStringLiteral("That end is shared with %1 other entity/entities. "
                                  "Extending it would either drag them along or leave a "
                                  "gap where they meet -- detach it first, or extend "
                                  "the other end.")
                       .arg(sharers));
  }

  // The reachable extension. For a segment that is the rest of the
  // infinite line; for an arc it is the remainder of its own circle,
  // which is why the search window closes at tFull rather than running
  // away.
  const double tMax = A.tFull();
  double best = forward ? std::numeric_limits<double>::infinity()
                        : -std::numeric_limits<double>::infinity();
  bool found = false;

  auto consider = [&](Complex pt) {
    double t = A.paramOf(pt);
    if (A.isArc) {
      // paramOf lands in [0, tFull); the gap past BOTH ends is the
      // interval (1, tFull), approached upward from n1 and downward
      // from n0.
      if (t <= 1.0 + tolParam || t >= tMax - tolParam)
        return;
      if (forward) {
        if (t < best) {
          best = t;
          found = true;
        }
      } else if (t > best) {
        best = t;
        found = true;
      }
      return;
    }
    if (forward) {
      if (t > 1.0 + tolParam && t < best) {
        best = t;
        found = true;
      }
    } else if (t < -tolParam && t > best) {
      best = t;
      found = true;
    }
  };

  for (int j = 0; j < p.segments.size(); j++) {
    if (kind == EntityKind::Segment && j == index)
      continue;
    Curve B;
    if (!buildCurve(p, EntityKind::Segment, j, B))
      continue;
    for (Complex pt : meetingPoints(A, B, false))
      consider(pt);
  }
  for (int j = 0; j < p.arcSegments.size(); j++) {
    if (kind == EntityKind::Arc && j == index)
      continue;
    Curve B;
    if (!buildCurve(p, EntityKind::Arc, j, B))
      continue;
    for (Complex pt : meetingPoints(A, B, false))
      consider(pt);
  }

  if (!found) {
    return failure(QStringLiteral("There is nothing in that direction to extend to. "
                                  "Extend reaches the first entity in the entity's own "
                                  "path -- it does not guess a length."));
  }

  Result r;
  r.affectedReferences = countReferences(p, kind, index);
  const Complex pt = A.at(best);
  p.nodes[endNode].x = pt.real();
  p.nodes[endNode].y = pt.imag();
  r.nodeA = endNode;

  if (kind == EntityKind::Arc) {
    // Same circle, longer sweep. Forward runs n0 -> best; backward
    // starts at `best`, crosses t=tFull (== t=0) and ends at the
    // original n1, so its span is what is left of the circle plus the
    // original arc.
    const double sweep = p.arcSegments[index].arcLength;
    p.arcSegments[index].arcLength = forward ? best * sweep : (tMax - best + 1.0) * sweep;
  }

  r.ok = true;
  if (r.affectedReferences > 0) {
    r.message = QStringLiteral("Extended. %1 constraint(s)/dimension(s) still refer to "
                               "this entity, which is now longer than when they were "
                               "created.")
                    .arg(r.affectedReferences);
  }
  return r;
}
