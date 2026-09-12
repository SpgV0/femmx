// tst_snap_engine.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12
// (issue #28).
//
// Snapping was grid-only. That is not just an ergonomic gap: to start a
// segment exactly at an existing node you either landed on the grid or
// placed it approximately and repaired it with a Coincident constraint
// afterwards -- and geometry that only LOOKS joined does not mesh as a
// bounded region.
//
// Most of this file is about PRIORITY rather than about whether each
// individual target can be computed. Computing a midpoint is easy;
// deciding that an endpoint 2 units away beats a midpoint 1 unit away is
// the design, and getting it wrong makes the specific targets
// unreachable wherever a generic one happens to be closer -- which, for
// the nearest-point-on-edge target, is almost everywhere along an edge.

#include <QtTest>

#include "FemmProblem.h"
#include "FemmProblemEdit.h"
#include "SnapEngine.h"

#include <cmath>

using SnapEngine::SnapType;

namespace {

constexpr double kTol = 1e-9;

// A 10x6 rectangle, plus a diagonal that crosses the bottom edge.
FemmProblem shapes()
{
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  const int n0 = FemmProblemEdit::addNode(p, 0, 0);
  const int n1 = FemmProblemEdit::addNode(p, 10, 0);
  const int n2 = FemmProblemEdit::addNode(p, 10, 6);
  const int n3 = FemmProblemEdit::addNode(p, 0, 6);
  FemmProblemEdit::addSegment(p, n0, n1); // 0: bottom, y=0
  FemmProblemEdit::addSegment(p, n1, n2); // 1: right, x=10
  FemmProblemEdit::addSegment(p, n2, n3); // 2: top
  FemmProblemEdit::addSegment(p, n3, n0); // 3: left
  return p;
}

QString describe(const SnapEngine::SnapResult& r)
{
  return QStringLiteral("%1 at (%2, %3) index %4")
      .arg(SnapEngine::name(r.type))
      .arg(r.x, 0, 'g', 12)
      .arg(r.y, 0, 'g', 12)
      .arg(r.index);
}

} // namespace

class TestSnapEngine : public QObject
{
  Q_OBJECT

private slots:
  void nothingInRangeLeavesTheCursorAlone();
  void gridSnapStillWorksWhereThereIsNoGeometry();

  void endpointIsFound();
  void midpointIsFound();
  void nearestPointOnASegmentIsFound();
  void arcCentreAndQuadrantsAreFound();
  void intersectionOfTwoSegmentsIsFound();

  void endpointBeatsACloserMidpoint();
  void endpointBeatsACloserPointOnTheEdge();
  void geometryBeatsTheGrid();
  void theNearestOfTwoEndpointsWins();

  void perpendicularNeedsAReferencePoint();
  void tangentNeedsAReferenceOutsideTheCircle();

  void aDisabledTypeIsNotOffered();
  void suspendingSnapEntirelyReturnsTheCursor();
  void aZeroCaptureRadiusSnapsToNothingGeometric();
};

// ---------------------------------------------------------------------------

void TestSnapEngine::nothingInRangeLeavesTheCursorAlone()
{
  FemmProblem p = shapes();
  const auto r = SnapEngine::findSnap(p, 500, 500, 0.5,
      SnapEngine::SnapEndpoint, 0.0);
  QCOMPARE((int)r.type, (int)SnapType::None);
  QCOMPARE(r.x, 500.0);
  QCOMPARE(r.y, 500.0);
  QVERIFY(!r.snapped());
}

void TestSnapEngine::gridSnapStillWorksWhereThereIsNoGeometry()
{
  // Composes with grid snap rather than replacing it -- the ticket's own
  // requirement. Far from any geometry, behaviour is exactly what it was
  // before object snapping existed.
  FemmProblem p = shapes();
  const auto r = SnapEngine::findSnap(p, 503.2, 497.4, 0.5,
      SnapEngine::SnapDefault, 5.0);
  QCOMPARE((int)r.type, (int)SnapType::Grid);
  QCOMPARE(r.x, 505.0);
  QCOMPARE(r.y, 495.0);
}

void TestSnapEngine::endpointIsFound()
{
  FemmProblem p = shapes();
  const auto r = SnapEngine::findSnap(p, 10.2, 0.1, 1.0,
      SnapEngine::SnapDefault, 0.0);
  QVERIFY2((int)r.type == (int)SnapType::Endpoint, qPrintable(describe(r)));
  QVERIFY(std::fabs(r.x - 10.0) < kTol);
  QVERIFY(std::fabs(r.y - 0.0) < kTol);
  QCOMPARE(r.index, 1);
}

void TestSnapEngine::midpointIsFound()
{
  FemmProblem p = shapes();
  // near the middle of the bottom edge, far from both its endpoints
  const auto r = SnapEngine::findSnap(p, 5.1, 0.2, 0.5,
      SnapEngine::SnapEndpoint | SnapEngine::SnapMidpoint, 0.0);
  QVERIFY2((int)r.type == (int)SnapType::Midpoint, qPrintable(describe(r)));
  QVERIFY(std::fabs(r.x - 5.0) < kTol);
  QVERIFY(std::fabs(r.y - 0.0) < kTol);
  QCOMPARE(r.index, 0);
}

void TestSnapEngine::nearestPointOnASegmentIsFound()
{
  FemmProblem p = shapes();
  const auto r = SnapEngine::findSnap(p, 3.0, 0.3, 0.5,
      SnapEngine::SnapOnEdge, 0.0);
  QVERIFY2((int)r.type == (int)SnapType::OnEdge, qPrintable(describe(r)));
  QVERIFY(std::fabs(r.x - 3.0) < kTol);
  QVERIFY(std::fabs(r.y - 0.0) < kTol);
  QCOMPARE(r.index, 0);
}

void TestSnapEngine::arcCentreAndQuadrantsAreFound()
{
  // A quarter arc from (1,0) to (0,1) about the origin.
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 1, 0);
  const int b = FemmProblemEdit::addNode(p, 0, 1);
  FemmProblemEdit::addArcSegment(p, a, b, 90.0, 1.0);

  const auto c = SnapEngine::findSnap(p, 0.1, 0.05, 0.5,
      SnapEngine::SnapCentre, 0.0);
  QVERIFY2((int)c.type == (int)SnapType::Centre, qPrintable(describe(c)));
  QVERIFY(std::fabs(c.x) < kTol && std::fabs(c.y) < kTol);

  // The 90-degree quadrant point, (0,1), is on this arc. Endpoint snap is
  // off so the quadrant is what answers -- (0,1) is also a node here.
  const auto q = SnapEngine::findSnap(p, 0.02, 0.98, 0.2,
      SnapEngine::SnapQuadrant, 0.0);
  QVERIFY2((int)q.type == (int)SnapType::Quadrant, qPrintable(describe(q)));
  QVERIFY(std::fabs(q.x) < kTol);
  QVERIFY(std::fabs(q.y - 1.0) < kTol);

  // A quadrant NOT on this arc's sweep must not be offered: 180 degrees
  // is at (-1,0), which the quarter arc never reaches.
  const auto off = SnapEngine::findSnap(p, -1.0, 0.0, 0.3,
      SnapEngine::SnapQuadrant, 0.0);
  QVERIFY2((int)off.type == (int)SnapType::None, qPrintable(describe(off)));
}

void TestSnapEngine::intersectionOfTwoSegmentsIsFound()
{
  // An X. The crossing point has no node on it, which is exactly why
  // snapping to it matters -- it cannot be reached by endpoint snap.
  FemmProblem p;
  const int a0 = FemmProblemEdit::addNode(p, 0, 0);
  const int a1 = FemmProblemEdit::addNode(p, 10, 10);
  const int b0 = FemmProblemEdit::addNode(p, 0, 10);
  const int b1 = FemmProblemEdit::addNode(p, 10, 0);
  FemmProblemEdit::addSegment(p, a0, a1);
  FemmProblemEdit::addSegment(p, b0, b1);

  const auto r = SnapEngine::findSnap(p, 5.2, 4.8, 0.6,
      SnapEngine::SnapIntersection, 0.0);
  QVERIFY2((int)r.type == (int)SnapType::Intersection, qPrintable(describe(r)));
  QVERIFY(std::fabs(r.x - 5.0) < kTol);
  QVERIFY(std::fabs(r.y - 5.0) < kTol);
  QCOMPARE(r.index, 0);
  QCOMPARE(r.otherIndex, 1);
}

// ---------------------------------------------------------------------------
// Priority -- the actual design
// ---------------------------------------------------------------------------

void TestSnapEngine::endpointBeatsACloserMidpoint()
{
  // A short segment whose midpoint is nearer the cursor than the corner
  // it shares with the rectangle. Nearest-wins would make the corner
  // unreachable from this side.
  FemmProblem p = shapes();
  const int extra = FemmProblemEdit::addNode(p, 0, -2);
  FemmProblemEdit::addSegment(p, 0, extra); // midpoint at (0,-1)

  // cursor is 0.2 from the midpoint (0,-1) and 0.8 from the corner (0,0)
  const auto r = SnapEngine::findSnap(p, 0.0, -0.8, 1.0,
      SnapEngine::SnapEndpoint | SnapEngine::SnapMidpoint, 0.0);
  QVERIFY2((int)r.type == (int)SnapType::Endpoint,
      qPrintable(QStringLiteral("a closer midpoint beat the endpoint: %1")
                     .arg(describe(r))));
  QVERIFY(std::fabs(r.y) < kTol);
}

void TestSnapEngine::endpointBeatsACloserPointOnTheEdge()
{
  // The worst case for nearest-wins: hovering just off an edge near a
  // corner, the nearest point ON the edge is always closer than the
  // corner itself, so a corner could never be snapped to at all.
  FemmProblem p = shapes();
  const auto r = SnapEngine::findSnap(p, 0.3, 0.05, 1.0,
      SnapEngine::SnapEndpoint | SnapEngine::SnapOnEdge, 0.0);
  QVERIFY2((int)r.type == (int)SnapType::Endpoint,
      qPrintable(QStringLiteral("the nearest point on the edge beat the "
                                "corner: %1").arg(describe(r))));
  QVERIFY(std::fabs(r.x) < kTol);
  QVERIFY(std::fabs(r.y) < kTol);
}

void TestSnapEngine::geometryBeatsTheGrid()
{
  // A node sitting off the grid must still win, or object snapping would
  // be pointless wherever the grid is fine enough.
  FemmProblem p;
  FemmProblemEdit::addNode(p, 3.3, 7.7);
  const auto r = SnapEngine::findSnap(p, 3.4, 7.6, 1.0,
      SnapEngine::SnapDefault, 1.0);
  QVERIFY2((int)r.type == (int)SnapType::Endpoint, qPrintable(describe(r)));
  QVERIFY(std::fabs(r.x - 3.3) < kTol);
  QVERIFY(std::fabs(r.y - 7.7) < kTol);
}

void TestSnapEngine::theNearestOfTwoEndpointsWins()
{
  // Within one type, distance decides -- the tie-break the priority rule
  // still needs.
  FemmProblem p;
  FemmProblemEdit::addNode(p, 0, 0);
  FemmProblemEdit::addNode(p, 2, 0);
  const auto r = SnapEngine::findSnap(p, 1.6, 0.0, 5.0,
      SnapEngine::SnapEndpoint, 0.0);
  QCOMPARE((int)r.type, (int)SnapType::Endpoint);
  QVERIFY(std::fabs(r.x - 2.0) < kTol);
  QCOMPARE(r.index, 1);
}

// ---------------------------------------------------------------------------
// The two that need somewhere to measure from
// ---------------------------------------------------------------------------

void TestSnapEngine::perpendicularNeedsAReferencePoint()
{
  // Drawing from (5,5) towards the bottom edge: the perpendicular foot is
  // (5,0). Without a reference point there is nothing to be perpendicular
  // FROM, so the target must not be offered at all.
  FemmProblem p = shapes();

  const auto without = SnapEngine::findSnap(p, 5.05, 0.1, 0.3,
      SnapEngine::SnapPerpendicular, 0.0);
  QVERIFY2((int)without.type == (int)SnapType::None,
      qPrintable(QStringLiteral("perpendicular was offered with no reference "
                                "point: %1").arg(describe(without))));

  const auto with = SnapEngine::findSnap(p, 5.05, 0.1, 0.3,
      SnapEngine::SnapPerpendicular, 0.0, true, 5.0, 5.0);
  QVERIFY2((int)with.type == (int)SnapType::Perpendicular,
      qPrintable(describe(with)));
  QVERIFY(std::fabs(with.x - 5.0) < kTol);
  QVERIFY(std::fabs(with.y - 0.0) < kTol);
}

void TestSnapEngine::tangentNeedsAReferenceOutsideTheCircle()
{
  // A full circle as two half arcs, radius 1 about the origin; drawing
  // from (3,0). The tangent touch points are real only because the
  // reference is outside the circle -- from inside there are none, and
  // offering one would be inventing geometry.
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 1, 0);
  const int b = FemmProblemEdit::addNode(p, -1, 0);
  FemmProblemEdit::addArcSegment(p, a, b, 180.0, 1.0);
  FemmProblemEdit::addArcSegment(p, b, a, 180.0, 1.0);

  // touch point for a tangent from (3,0): x = R^2/d = 1/3
  const double tx = 1.0 / 3.0;
  const double ty = std::sqrt(1.0 - tx * tx);

  const auto outside = SnapEngine::findSnap(p, tx + 0.02, ty + 0.02, 0.2,
      SnapEngine::SnapTangent, 0.0, true, 3.0, 0.0);
  QVERIFY2((int)outside.type == (int)SnapType::Tangent,
      qPrintable(describe(outside)));
  QVERIFY(std::fabs(std::fabs(outside.x) - tx) < 1e-9);
  QVERIFY(std::fabs(std::fabs(outside.y) - ty) < 1e-9);

  // From the centre there is no tangent at all.
  const auto inside = SnapEngine::findSnap(p, tx, ty, 0.5,
      SnapEngine::SnapTangent, 0.0, true, 0.0, 0.0);
  QVERIFY2((int)inside.type == (int)SnapType::None,
      qPrintable(QStringLiteral("a tangent was offered from inside the "
                                "circle: %1").arg(describe(inside))));
}

// ---------------------------------------------------------------------------
// Turning it off
// ---------------------------------------------------------------------------

void TestSnapEngine::aDisabledTypeIsNotOffered()
{
  FemmProblem p = shapes();
  // right on a corner, but endpoints disabled: the next-best target for
  // this cursor is the edge itself
  const auto r = SnapEngine::findSnap(p, 0.0, 0.0, 1.0,
      SnapEngine::SnapOnEdge, 0.0);
  QVERIFY2((int)r.type == (int)SnapType::OnEdge, qPrintable(describe(r)));
}

void TestSnapEngine::suspendingSnapEntirelyReturnsTheCursor()
{
  // What the modifier key does: no flags, so nothing snaps, even sitting
  // exactly on a node.
  FemmProblem p = shapes();
  const auto r = SnapEngine::findSnap(p, 0.0, 0.0, 5.0,
      SnapEngine::SnapNone, 1.0);
  QCOMPARE((int)r.type, (int)SnapType::None);
  QCOMPARE(r.x, 0.0);
  QCOMPARE(r.y, 0.0);
}

void TestSnapEngine::aZeroCaptureRadiusSnapsToNothingGeometric()
{
  // The capture radius arrives in model units, converted by the caller
  // from pixels. A degenerate view scale must not make everything snap
  // to the nearest node in the model.
  FemmProblem p = shapes();
  const auto r = SnapEngine::findSnap(p, 4.0, 4.0, 0.0,
      SnapEngine::SnapDefault, 0.0);
  QCOMPARE((int)r.type, (int)SnapType::None);
  QCOMPARE(r.x, 4.0);
  QCOMPARE(r.y, 4.0);
}

QTEST_GUILESS_MAIN(TestSnapEngine)
#include "tst_snap_engine.moc"
