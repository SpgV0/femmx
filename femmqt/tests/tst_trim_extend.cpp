// tst_trim_extend.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12
// (issue #29).
//
// Trim, extend and split are geometry, and geometry is where a wrong
// answer looks like a right one. #25 is the standing example in this
// repo: a hand-derived expected arc centre was wrong in the same
// direction as the code that produced it, and a save/reload test passed
// throughout because it compared the broken derivation against itself.
//
// So the arc cases here check the DEFINING PROPERTY rather than a
// coordinate I worked out by hand: after a split or an extend, each
// resulting arc must still lie on the same circle -- same centre, same
// radius -- as the arc it came from, according to circleFromArc, which
// is what every other part of the editor asks. A sign error anywhere in
// the parameterisation moves the centre, and no arithmetic of mine has
// to be trusted for the test to catch it.

#include <QtTest>

#include "FemmProblem.h"
#include "FemmProblemEdit.h"
#include "TrimExtend.h"

#include <cmath>
#include <complex>

using TrimExtend::EntityKind;

namespace {

// A horizontal line from (0,0) to (10,0), crossed by two verticals at
// x = 3 and x = 7. The classic trim picture: three pieces, and which one
// goes depends only on where you click.
FemmProblem crossedLine()
{
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  FemmProblemEdit::addSegment(p, a, b); // segment 0, the one under test

  const int c = FemmProblemEdit::addNode(p, 3, -2);
  const int d = FemmProblemEdit::addNode(p, 3, 2);
  FemmProblemEdit::addSegment(p, c, d); // segment 1

  const int e = FemmProblemEdit::addNode(p, 7, -2);
  const int f = FemmProblemEdit::addNode(p, 7, 2);
  FemmProblemEdit::addSegment(p, e, f); // segment 2
  return p;
}

std::complex<double> centreOf(const FemmProblem& p, int arcIndex, double& R)
{
  std::complex<double> c;
  R = 0;
  const bool ok = FemmProblemEdit::circleFromArc(p, p.arcSegments[arcIndex], c, R);
  return ok ? c : std::complex<double>(std::nan(""), std::nan(""));
}

bool nodeIsAt(const FemmProblem& p, int index, double x, double y, double tol = 1e-9)
{
  return index >= 0 && index < p.nodes.size()
      && std::abs(p.nodes[index].x - x) < tol && std::abs(p.nodes[index].y - y) < tol;
}

} // namespace

class TestTrimExtend : public QObject
{
  Q_OBJECT

  private slots:
  void cutsAreSortedInteriorAndDeduplicated();
  void aTJunctionCountsAsACut();
  void anUnrelatedNodeOnTheLineCountsAsACut();

  void splittingASegmentGivesTwoHalvesThatInheritEverything();
  void splittingAtAnEndIsRefused();
  void splittingReusesANodeAlreadyThere();
  void splittingAnArcKeepsBothHalvesOnTheSameCircle();

  void trimmingTheMiddleLeavesThePiecesEitherSide();
  void trimmingAnOverhangShortensTheLine();
  void trimmingAnUnboundedEntityRemovesAllOfIt();
  void trimmingKeepsThePropertiesOfWhatSurvives();
  void trimmingAnArcKeepsTheRemainderOnTheSameCircle();

  void extendingReachesTheFirstEntityInItsPath();
  void extendingStopsAtTheNEARESTEntityNotTheFurthest();
  void extendingASharedEndIsRefused();
  void extendingIntoNothingIsRefused();
  void extendingAnArcGrowsTheSweepAndKeepsTheCircle();
  void extendingAnArcBackwardsFromItsStartAlsoKeepsTheCircle();
};

// ---------------------------------------------------------------------------
// What counts as a cut
// ---------------------------------------------------------------------------

void TestTrimExtend::cutsAreSortedInteriorAndDeduplicated()
{
  FemmProblem p = crossedLine();
  const QVector<double> cuts = TrimExtend::cutParameters(p, EntityKind::Segment, 0);
  QCOMPARE(cuts.size(), 2);
  QVERIFY(cuts[0] < cuts[1]);
  QVERIFY(std::abs(cuts[0] - 0.3) < 1e-9);
  QVERIFY(std::abs(cuts[1] - 0.7) < 1e-9);
}

void TestTrimExtend::aTJunctionCountsAsACut()
{
  // FemmProblemEdit::splitIntersectingSegments deliberately ignores this
  // case -- an end-to-middle touch is not a proper crossing, and its job
  // is different. For trim it is the commonest cut there is, because it
  // is exactly what endpoint snapping (#28) produces.
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  FemmProblemEdit::addSegment(p, a, b);
  const int c = FemmProblemEdit::addNode(p, 4, 0); // ON the line, not past it
  const int d = FemmProblemEdit::addNode(p, 4, 5);
  FemmProblemEdit::addSegment(p, c, d);

  const QVector<double> cuts = TrimExtend::cutParameters(p, EntityKind::Segment, 0);
  QVERIFY2(cuts.size() == 1,
      qPrintable(QStringLiteral("expected the T-junction at x=4 to cut the line, got %1 cuts")
                     .arg(cuts.size())));
  QVERIFY(std::abs(cuts[0] - 0.4) < 1e-9);
}

void TestTrimExtend::anUnrelatedNodeOnTheLineCountsAsACut()
{
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  FemmProblemEdit::addSegment(p, a, b);
  FemmProblemEdit::addNode(p, 6, 0); // a bare node the user placed on it

  const QVector<double> cuts = TrimExtend::cutParameters(p, EntityKind::Segment, 0);
  QCOMPARE(cuts.size(), 1);
  QVERIFY(std::abs(cuts[0] - 0.6) < 1e-9);
}

// ---------------------------------------------------------------------------
// Split
// ---------------------------------------------------------------------------

void TestTrimExtend::splittingASegmentGivesTwoHalvesThatInheritEverything()
{
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  const int s = FemmProblemEdit::addSegment(p, a, b);
  // Properties that must survive: losing a boundary condition on half a
  // line changes the physics, silently.
  p.segments[s].boundaryMarker = 3;
  p.segments[s].inGroup = 7;
  p.segments[s].maxSideLength = 0.25;
  p.segments[s].hidden = true;

  const auto r = TrimExtend::split(p, EntityKind::Segment, s, 5, 0);
  QVERIFY2(r.ok, qPrintable(r.message));
  QCOMPARE(p.segments.size(), 2);
  QVERIFY(nodeIsAt(p, r.nodeA, 5, 0));

  for (const FemmSegment& seg : p.segments) {
    QCOMPARE(seg.boundaryMarker, 3);
    QCOMPARE(seg.inGroup, 7);
    QCOMPARE(seg.maxSideLength, 0.25);
    QCOMPARE(seg.hidden, true);
  }
  // The two halves must be joined at the new node, not merely adjacent.
  QCOMPARE(p.segments[0].n0, a);
  QCOMPARE(p.segments[0].n1, r.nodeA);
  QCOMPARE(p.segments[1].n0, r.nodeA);
  QCOMPARE(p.segments[1].n1, b);
}

void TestTrimExtend::splittingAtAnEndIsRefused()
{
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  FemmProblemEdit::addSegment(p, a, b);

  const auto r = TrimExtend::split(p, EntityKind::Segment, 0, 0, 0);
  QVERIFY2(!r.ok, "splitting at an endpoint produced a zero-length piece "
                  "instead of being refused");
  QVERIFY(!r.message.isEmpty());
  QCOMPARE(p.segments.size(), 1);
}

void TestTrimExtend::splittingReusesANodeAlreadyThere()
{
  // A duplicate node on top of an existing one is the classic
  // looks-joined-but-does-not-mesh failure: the mesher sees two
  // coincident vertices and the region they appear to bound is not
  // bounded.
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  FemmProblemEdit::addSegment(p, a, b);
  const int existing = FemmProblemEdit::addNode(p, 5, 0);
  const int before = p.nodes.size();

  const auto r = TrimExtend::split(p, EntityKind::Segment, 0, 5, 0);
  QVERIFY2(r.ok, qPrintable(r.message));
  QCOMPARE(r.nodeA, existing);
  QCOMPARE(p.nodes.size(), before);
}

void TestTrimExtend::splittingAnArcKeepsBothHalvesOnTheSameCircle()
{
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 1, 0);
  const int b = FemmProblemEdit::addNode(p, 0, 1);
  FemmProblemEdit::addArcSegment(p, a, b, 90.0, 1.0);
  p.arcSegments[0].boundaryMarker = 2;
  p.arcSegments[0].inGroup = 5;

  double R0 = 0;
  const std::complex<double> c0 = centreOf(p, 0, R0);

  // The midpoint of a 90-degree arc centred on the origin, radius 1.
  const double m = std::sqrt(0.5);
  const auto r = TrimExtend::split(p, EntityKind::Arc, 0, m, m);
  QVERIFY2(r.ok, qPrintable(r.message));
  QCOMPARE(p.arcSegments.size(), 2);

  // The included angles must add back up to the original.
  QVERIFY(std::abs(p.arcSegments[0].arcLength + p.arcSegments[1].arcLength - 90.0) < 1e-9);

  // The property that matters, and the one a hand-computed coordinate
  // would not have caught: both halves still describe the SAME circle.
  for (int i = 0; i < 2; i++) {
    double R = 0;
    const std::complex<double> c = centreOf(p, i, R);
    QVERIFY2(std::abs(c - c0) < 1e-9,
        qPrintable(QStringLiteral("half %1 sits on a different circle: centre (%2,%3) "
                                  "instead of (%4,%5)")
                       .arg(i).arg(c.real()).arg(c.imag()).arg(c0.real()).arg(c0.imag())));
    QVERIFY(std::abs(R - R0) < 1e-9);
    QCOMPARE(p.arcSegments[i].boundaryMarker, 2);
    QCOMPARE(p.arcSegments[i].inGroup, 5);
  }
}

// ---------------------------------------------------------------------------
// Trim
// ---------------------------------------------------------------------------

void TestTrimExtend::trimmingTheMiddleLeavesThePiecesEitherSide()
{
  FemmProblem p = crossedLine();
  const auto r = TrimExtend::trim(p, EntityKind::Segment, 0, 5, 0); // between the verticals
  QVERIFY2(r.ok, qPrintable(r.message));

  // Two verticals plus the two surviving pieces of the horizontal.
  QCOMPARE(p.segments.size(), 4);
  QVERIFY(nodeIsAt(p, r.nodeA, 3, 0));
  QVERIFY(nodeIsAt(p, r.nodeB, 7, 0));

  // Nothing may remain that covers the trimmed gap.
  for (const FemmSegment& s : p.segments) {
    const double x0 = p.nodes[s.n0].x, y0 = p.nodes[s.n0].y;
    const double x1 = p.nodes[s.n1].x, y1 = p.nodes[s.n1].y;
    if (y0 == 0 && y1 == 0) {
      const double lo = std::min(x0, x1), hi = std::max(x0, x1);
      QVERIFY2(!(lo < 4.9 && hi > 5.1),
          "a horizontal piece still spans x=5, which is the piece that was trimmed");
    }
  }
}

void TestTrimExtend::trimmingAnOverhangShortensTheLine()
{
  FemmProblem p = crossedLine();
  // Past the last crossing: the tail from x=7 to x=10.
  const auto r = TrimExtend::trim(p, EntityKind::Segment, 0, 9, 0);
  QVERIFY2(r.ok, qPrintable(r.message));
  QCOMPARE(p.segments.size(), 3); // no new piece, just a shorter one
  QVERIFY(nodeIsAt(p, p.segments[0].n0, 0, 0));
  QVERIFY(nodeIsAt(p, p.segments[0].n1, 7, 0));
}

void TestTrimExtend::trimmingAnUnboundedEntityRemovesAllOfIt()
{
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  FemmProblemEdit::addSegment(p, a, b);

  const auto r = TrimExtend::trim(p, EntityKind::Segment, 0, 5, 0);
  QVERIFY2(r.ok, qPrintable(r.message));
  QCOMPARE(p.segments.size(), 0);
  QVERIFY2(!r.message.isEmpty(),
      "removing the whole entity is a surprising enough outcome that it has to say so");
}

void TestTrimExtend::trimmingKeepsThePropertiesOfWhatSurvives()
{
  FemmProblem p = crossedLine();
  p.segments[0].boundaryMarker = 4;
  p.segments[0].inGroup = 9;
  p.segments[0].maxSideLength = 0.5;

  const auto r = TrimExtend::trim(p, EntityKind::Segment, 0, 5, 0);
  QVERIFY(r.ok);
  int checked = 0;
  for (const FemmSegment& s : p.segments) {
    if (p.nodes[s.n0].y == 0 && p.nodes[s.n1].y == 0) {
      QCOMPARE(s.boundaryMarker, 4);
      QCOMPARE(s.inGroup, 9);
      QCOMPARE(s.maxSideLength, 0.5);
      checked++;
    }
  }
  QCOMPARE(checked, 2);
}

void TestTrimExtend::trimmingAnArcKeepsTheRemainderOnTheSameCircle()
{
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 1, 0);
  const int b = FemmProblemEdit::addNode(p, -1, 0);
  FemmProblemEdit::addArcSegment(p, a, b, 180.0, 1.0); // upper half of the unit circle

  double R0 = 0;
  const std::complex<double> c0 = centreOf(p, 0, R0);

  // A vertical line crossing the arc at (0,1) -- its own top.
  const int c = FemmProblemEdit::addNode(p, 0, 0.5);
  const int d = FemmProblemEdit::addNode(p, 0, 2);
  FemmProblemEdit::addSegment(p, c, d);

  // Click on the second quarter (x negative, y positive).
  const double q = std::sqrt(0.5);
  const auto r = TrimExtend::trim(p, EntityKind::Arc, 0, -q, q);
  QVERIFY2(r.ok, qPrintable(r.message));
  QCOMPARE(p.arcSegments.size(), 1);
  QVERIFY2(std::abs(p.arcSegments[0].arcLength - 90.0) < 1e-6,
      qPrintable(QStringLiteral("expected a quarter left, got %1 degrees")
                     .arg(p.arcSegments[0].arcLength)));

  double R = 0;
  const std::complex<double> cc = centreOf(p, 0, R);
  QVERIFY2(std::abs(cc - c0) < 1e-9,
      "the surviving quarter is on a different circle than the arc it came from");
  QVERIFY(std::abs(R - R0) < 1e-9);
}

// ---------------------------------------------------------------------------
// Extend
// ---------------------------------------------------------------------------

void TestTrimExtend::extendingReachesTheFirstEntityInItsPath()
{
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 4, 0);
  FemmProblemEdit::addSegment(p, a, b);
  const int c = FemmProblemEdit::addNode(p, 9, -3);
  const int d = FemmProblemEdit::addNode(p, 9, 3);
  FemmProblemEdit::addSegment(p, c, d);

  const auto r = TrimExtend::extend(p, EntityKind::Segment, 0, 3.9, 0);
  QVERIFY2(r.ok, qPrintable(r.message));
  QVERIFY2(nodeIsAt(p, b, 9, 0),
      "the extended end did not land exactly on the line it was extended to");
  // Extending must not invent geometry -- it moves an end, nothing more.
  QCOMPARE(p.segments.size(), 2);
}

void TestTrimExtend::extendingStopsAtTheNEARESTEntityNotTheFurthest()
{
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 4, 0);
  FemmProblemEdit::addSegment(p, a, b);
  for (double xWall : { 20.0, 9.0, 14.0 }) { // deliberately out of order
    const int c = FemmProblemEdit::addNode(p, xWall, -3);
    const int d = FemmProblemEdit::addNode(p, xWall, 3);
    FemmProblemEdit::addSegment(p, c, d);
  }

  const auto r = TrimExtend::extend(p, EntityKind::Segment, 0, 3.9, 0);
  QVERIFY2(r.ok, qPrintable(r.message));
  QVERIFY2(nodeIsAt(p, b, 9, 0),
      "extend ran past the nearest wall -- it must stop at the first entity "
      "in its path, not the last one it happened to test");
}

void TestTrimExtend::extendingASharedEndIsRefused()
{
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 4, 0);
  FemmProblemEdit::addSegment(p, a, b);
  const int up = FemmProblemEdit::addNode(p, 4, 4);
  FemmProblemEdit::addSegment(p, b, up); // shares the end being extended
  const int c = FemmProblemEdit::addNode(p, 9, -3);
  const int d = FemmProblemEdit::addNode(p, 9, 3);
  FemmProblemEdit::addSegment(p, c, d);

  const auto r = TrimExtend::extend(p, EntityKind::Segment, 0, 3.9, 0);
  QVERIFY2(!r.ok,
      "extending a shared end silently dragged the attached geometry along "
      "or tore the junction open");
  QVERIFY(nodeIsAt(p, b, 4, 0)); // unchanged
  QVERIFY(!r.message.isEmpty());
}

void TestTrimExtend::extendingIntoNothingIsRefused()
{
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 4, 0);
  FemmProblemEdit::addSegment(p, a, b);

  const auto r = TrimExtend::extend(p, EntityKind::Segment, 0, 3.9, 0);
  QVERIFY2(!r.ok, "extend invented a length rather than refusing");
  QVERIFY(nodeIsAt(p, b, 4, 0));
}

void TestTrimExtend::extendingAnArcGrowsTheSweepAndKeepsTheCircle()
{
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 1, 0);
  const int b = FemmProblemEdit::addNode(p, 0, 1);
  FemmProblemEdit::addArcSegment(p, a, b, 90.0, 1.0); // first quadrant

  double R0 = 0;
  const std::complex<double> c0 = centreOf(p, 0, R0);

  // A horizontal line across the third/second quadrant boundary: the
  // continuing circle meets it at (-1, 0), i.e. 90 degrees further on.
  const int c = FemmProblemEdit::addNode(p, -3, 0);
  const int d = FemmProblemEdit::addNode(p, -0.5, 0);
  FemmProblemEdit::addSegment(p, c, d);

  const auto r = TrimExtend::extend(p, EntityKind::Arc, 0, 0.05, 0.99);
  QVERIFY2(r.ok, qPrintable(r.message));
  QVERIFY2(std::abs(p.arcSegments[0].arcLength - 180.0) < 1e-6,
      qPrintable(QStringLiteral("expected the sweep to grow to 180 degrees, got %1")
                     .arg(p.arcSegments[0].arcLength)));
  QVERIFY(nodeIsAt(p, b, -1, 0, 1e-9));

  double R = 0;
  const std::complex<double> cc = centreOf(p, 0, R);
  QVERIFY2(std::abs(cc - c0) < 1e-9,
      "extending the arc moved its centre -- the extension is not on the "
      "same circle the arc was drawn on");
  QVERIFY(std::abs(R - R0) < 1e-9);
}

void TestTrimExtend::extendingAnArcBackwardsFromItsStartAlsoKeepsTheCircle()
{
  // Growing an arc from its n0 end is the one case whose formula does
  // not fall out of the parameterisation directly: the new sweep has to
  // cross the arc's own t=0 to get back to the untouched n1, so it is
  // (tFull - t + 1) * arcLength rather than anything simpler. That is a
  // hand derivation, which is exactly what this file's header says not
  // to trust -- so it is checked against the circle it must stay on, and
  // against an obstacle placed so that getting the direction backwards
  // would pick a different one.
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 1, 0);
  const int b = FemmProblemEdit::addNode(p, 0, 1);
  FemmProblemEdit::addArcSegment(p, a, b, 90.0, 1.0); // first quadrant

  double R0 = 0;
  const std::complex<double> c0 = centreOf(p, 0, R0);

  // Along y = -x, so the unit circle meets it at (0.7071, -0.7071):
  // 45 degrees back from n0.
  const int c = FemmProblemEdit::addNode(p, 0.35, -0.35);
  const int d = FemmProblemEdit::addNode(p, 1.4, -1.4);
  FemmProblemEdit::addSegment(p, c, d);

  // Further round, at (0,-1): 90 degrees back. Extend must stop at the
  // first one it reaches, which is the 45-degree one.
  const int e = FemmProblemEdit::addNode(p, 0, -2);
  const int f = FemmProblemEdit::addNode(p, 0, -0.5);
  FemmProblemEdit::addSegment(p, e, f);

  const auto r = TrimExtend::extend(p, EntityKind::Arc, 0, 0.99, 0.05); // near n0
  QVERIFY2(r.ok, qPrintable(r.message));

  const double q = std::sqrt(0.5);
  QVERIFY2(nodeIsAt(p, a, q, -q, 1e-9),
      "the start did not land on the nearest obstacle going backwards");
  QVERIFY2(nodeIsAt(p, b, 0, 1),
      "extending from n0 moved n1, which must not have been touched");
  QVERIFY2(std::abs(p.arcSegments[0].arcLength - 135.0) < 1e-6,
      qPrintable(QStringLiteral("expected 90 + 45 = 135 degrees, got %1")
                     .arg(p.arcSegments[0].arcLength)));

  double R = 0;
  const std::complex<double> cc = centreOf(p, 0, R);
  QVERIFY2(std::abs(cc - c0) < 1e-9,
      "the backwards extension is not on the circle the arc was drawn on");
  QVERIFY(std::abs(R - R0) < 1e-9);
}

QTEST_GUILESS_MAIN(TestTrimExtend)
#include "tst_trim_extend.moc"
