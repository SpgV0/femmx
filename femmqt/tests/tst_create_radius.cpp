// tst_create_radius.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12
// (issue #24).
//
// Create Radius is femmqt's only true CAD construction operation: it
// rewrites topology, turning one corner node into an arc plus two
// trimmed edges. #13 covered add/delete/transform, which move entities
// around without changing how many there are; this changes the graph,
// and its failure modes are different -- a fillet that is almost tangent
// looks perfectly fine on screen and meshes into a sliver.
//
// Everything here is measured against geometry rather than against the
// implementation's own intermediate values: the arc is reconstructed
// into a centre and radius with circleFromArc() and then checked for
// tangency by distance, so an arc that happens to end in the right place
// while bulging the wrong way cannot pass.

#include <QtTest>

#include "FemmFileIO.h"
#include "FemmProblem.h"
#include "FemmProblemEdit.h"

#include <QTemporaryDir>

#include <cmath>
#include <complex>

namespace {

// Tangency and radius are compared at this scale. Not arbitrary:
// createRadius' own tolerance is r/10000 (it inherits femm/FemmeDoc.cpp's
// fixed ratio), so anything looser than that would not be checking the
// construction, and anything tighter would be checking double rounding.
constexpr double kGeomTol = 1e-9;

int addCorner(FemmProblem& p, double ax, double ay,
    double cx, double cy, double bx, double by)
{
  const int a = FemmProblemEdit::addNode(p, ax, ay);
  const int c = FemmProblemEdit::addNode(p, cx, cy);
  const int b = FemmProblemEdit::addNode(p, bx, by);
  FemmProblemEdit::addSegment(p, a, c);
  FemmProblemEdit::addSegment(p, c, b);
  return c;
}

// Distance from a point to the infinite line through (x0,y0)-(x1,y1).
double distanceToLine(double px, double py,
    double x0, double y0, double x1, double y1)
{
  const double dx = x1 - x0, dy = y1 - y0;
  const double len = std::hypot(dx, dy);
  return std::fabs((px - x0) * dy - (py - y0) * dx) / len;
}

} // namespace

class TestCreateRadius : public QObject
{
  Q_OBJECT

private slots:
  void canCreateRadiusNeedsExactlyTwoIncidentEdges();

  void filletOnARightAngleIsTangentAndTheRightRadius();
  void filletAcrossARangeOfAngles();
  void filletAcrossARangeOfAngles_data();

  void filletRemovesTheCornerAndTrimsBothEdges();
  void filletRejectsARadiusThatDoesNotFit();
  void filletRejectsCollinearEdges();
  void filletRejectsANonPositiveRadius();
  void filletRejectsAJunctionOfThreeEdges();

  void filletedCornerSurvivesASaveAndReload();

  void boundaryAndGroupAreCarriedOntoTheArc();
  void boundaryChoiceDependsOnCornerWinding();

  void filletDoesNotLeaveTheSketchPointingAtTheWrongGeometry();
  void filletDropsConstraintsOnTheCornerItRemoved();
  void deletingANodeRenumbersDimensionReferences();
  void deletingASegmentRenumbersConstraintReferences();
};

// ---------------------------------------------------------------------------

void TestCreateRadius::canCreateRadiusNeedsExactlyTwoIncidentEdges()
{
  FemmProblem p;
  const int corner = addCorner(p, 0, 10, 0, 0, 10, 0);
  QVERIFY(FemmProblemEdit::canCreateRadius(p, corner));

  // an endpoint of a single segment: one incident edge, not a corner
  FemmProblem q;
  const int a = FemmProblemEdit::addNode(q, 0, 0);
  const int b = FemmProblemEdit::addNode(q, 10, 0);
  FemmProblemEdit::addSegment(q, a, b);
  QVERIFY(!FemmProblemEdit::canCreateRadius(q, a));

  // an isolated node
  const int lonely = FemmProblemEdit::addNode(q, 50, 50);
  QVERIFY(!FemmProblemEdit::canCreateRadius(q, lonely));
}

void TestCreateRadius::filletOnARightAngleIsTangentAndTheRightRadius()
{
  // A 90-degree corner at the origin, legs along +y and +x.
  FemmProblem p;
  const int corner = addCorner(p, 0, 10, 0, 0, 10, 0);
  const double r = 2.0;

  QVERIFY(FemmProblemEdit::createRadius(p, corner, r));
  QCOMPARE(p.arcSegments.size(), 1);

  std::complex<double> centre;
  double radius = 0;
  QVERIFY(FemmProblemEdit::circleFromArc(p, p.arcSegments[0], centre, radius));

  // The radius the user asked for, not merely "an arc".
  QVERIFY2(std::fabs(radius - r) < kGeomTol,
      qPrintable(QStringLiteral("arc radius %1, expected %2")
                     .arg(radius, 0, 'g', 17).arg(r)));

  // For a 90-degree corner the centre must sit at (r, r): the only point
  // that is r from both legs on the inside.
  QVERIFY(std::fabs(centre.real() - r) < kGeomTol);
  QVERIFY(std::fabs(centre.imag() - r) < kGeomTol);

  // Tangency, stated as the thing tangency actually means: the centre is
  // exactly one radius from each leg.
  QVERIFY(std::fabs(distanceToLine(centre.real(), centre.imag(),
                        0, 0, 0, 10) - r) < kGeomTol);
  QVERIFY(std::fabs(distanceToLine(centre.real(), centre.imag(),
                        0, 0, 10, 0) - r) < kGeomTol);
}

void TestCreateRadius::filletAcrossARangeOfAngles_data()
{
  QTest::addColumn<double>("includedAngleDeg");
  QTest::addColumn<double>("radius");

  // Deliberately spanning the awkward ends. A very acute corner needs a
  // long trim (len = r/tan(phi/2) grows without bound as phi -> 0), and a
  // near-straight one needs an almost-flat arc; both are where a fillet
  // that is only approximately tangent stops looking approximately right.
  QTest::newRow("10 deg (very acute)") << 10.0 << 0.05;
  QTest::newRow("30 deg") << 30.0 << 0.5;
  QTest::newRow("45 deg") << 45.0 << 1.0;
  QTest::newRow("60 deg") << 60.0 << 1.5;
  QTest::newRow("90 deg") << 90.0 << 2.0;
  QTest::newRow("120 deg") << 120.0 << 2.0;
  QTest::newRow("150 deg") << 150.0 << 1.0;
  QTest::newRow("170 deg (near straight)") << 170.0 << 0.5;
}

void TestCreateRadius::filletAcrossARangeOfAngles()
{
  QFETCH(double, includedAngleDeg);
  QFETCH(double, radius);

  // Corner at the origin: one leg along +x, the other rotated by the
  // included angle. Legs are long enough that the trim always fits.
  const double phi = includedAngleDeg * M_PI / 180.0;
  const double legLen = 100.0;

  FemmProblem p;
  const int corner = addCorner(p,
      legLen, 0.0,
      0.0, 0.0,
      legLen * std::cos(phi), legLen * std::sin(phi));

  QVERIFY2(FemmProblemEdit::createRadius(p, corner, radius),
      qPrintable(QStringLiteral("createRadius refused a %1-degree corner "
                                "with radius %2 and %3-long legs")
                     .arg(includedAngleDeg).arg(radius).arg(legLen)));
  QCOMPARE(p.arcSegments.size(), 1);

  std::complex<double> centre;
  double got = 0;
  QVERIFY(FemmProblemEdit::circleFromArc(p, p.arcSegments[0], centre, got));

  QVERIFY2(std::fabs(got - radius) < kGeomTol,
      qPrintable(QStringLiteral("%1-degree corner: arc radius %2, asked %3")
                     .arg(includedAngleDeg).arg(got, 0, 'g', 17).arg(radius)));

  // Tangent to both original legs, which still lie along the same two
  // infinite lines even though their endpoints have been trimmed.
  const double d1 = distanceToLine(centre.real(), centre.imag(),
      0, 0, legLen, 0);
  const double d2 = distanceToLine(centre.real(), centre.imag(),
      0, 0, legLen * std::cos(phi), legLen * std::sin(phi));
  QVERIFY2(std::fabs(d1 - radius) < kGeomTol,
      qPrintable(QStringLiteral("%1-degree corner: centre is %2 from the "
                                "first leg, expected %3")
                     .arg(includedAngleDeg).arg(d1, 0, 'g', 17).arg(radius)));
  QVERIFY2(std::fabs(d2 - radius) < kGeomTol,
      qPrintable(QStringLiteral("%1-degree corner: centre is %2 from the "
                                "second leg, expected %3")
                     .arg(includedAngleDeg).arg(d2, 0, 'g', 17).arg(radius)));

  // The arc must span the corner, not its explement: the stored included
  // angle is 180 - phi for a fillet.
  const double expectedSweep = 180.0 - includedAngleDeg;
  QVERIFY2(std::fabs(p.arcSegments[0].arcLength - expectedSweep) < 1e-6,
      qPrintable(QStringLiteral("%1-degree corner: arc sweeps %2, expected %3")
                     .arg(includedAngleDeg)
                     .arg(p.arcSegments[0].arcLength).arg(expectedSweep)));
}

void TestCreateRadius::filletRemovesTheCornerAndTrimsBothEdges()
{
  FemmProblem p;
  const int corner = addCorner(p, 0, 10, 0, 0, 10, 0);
  QCOMPARE(p.nodes.size(), 3);
  QCOMPARE(p.segments.size(), 2);

  const double r = 2.0;
  QVERIFY(FemmProblemEdit::createRadius(p, corner, r));

  // The corner is gone and two tangent points have replaced it.
  QCOMPARE(p.nodes.size(), 4);
  QCOMPARE(p.segments.size(), 2);
  QCOMPARE(p.arcSegments.size(), 1);

  for (const FemmNode& node : p.nodes) {
    QVERIFY2(std::hypot(node.x, node.y) > kGeomTol,
        "the original corner node is still in the model");
  }

  // Both edges survived, trimmed rather than deleted -- the failure this
  // pins is a real one: deleteNode() cascades to anything referencing the
  // node, so filleting used to take both adjoining edges with it unless
  // they were rewired first (see the note in createRadius).
  QCOMPARE(p.segments.size(), 2);
  const double len = r / std::tan((90.0 * M_PI / 180.0) / 2.0);
  bool sawXLeg = false, sawYLeg = false;
  for (const FemmSegment& s : p.segments) {
    const FemmNode& a = p.nodes[s.n0];
    const FemmNode& b = p.nodes[s.n1];
    const double segLen = std::hypot(b.x - a.x, b.y - a.y);
    QVERIFY2(std::fabs(segLen - (10.0 - len)) < kGeomTol,
        qPrintable(QStringLiteral("a leg is %1 long; a 10-long leg trimmed "
                                  "by %2 should be %3")
                       .arg(segLen).arg(len).arg(10.0 - len)));
    if (std::fabs(a.y) < kGeomTol && std::fabs(b.y) < kGeomTol)
      sawXLeg = true;
    if (std::fabs(a.x) < kGeomTol && std::fabs(b.x) < kGeomTol)
      sawYLeg = true;
  }
  QVERIFY2(sawXLeg && sawYLeg, "the trimmed legs no longer lie on their "
                               "original lines");
}

void TestCreateRadius::filletRejectsARadiusThatDoesNotFit()
{
  // len = r/tan(phi/2); for a 90-degree corner that is just r, so a
  // radius larger than the shorter leg cannot be tangent to both.
  FemmProblem p;
  const int corner = addCorner(p, 0, 3, 0, 0, 10, 0);

  const FemmProblem before = p;
  QVERIFY2(!FemmProblemEdit::createRadius(p, corner, 5.0),
      "a fillet larger than the shorter leg was accepted");

  // Refusing must not half-do it. A rejected operation that has already
  // added nodes leaves the model worse than not trying.
  QCOMPARE(p.nodes.size(), before.nodes.size());
  QCOMPARE(p.segments.size(), before.segments.size());
  QCOMPARE(p.arcSegments.size(), before.arcSegments.size());
}

void TestCreateRadius::filletRejectsCollinearEdges()
{
  // Three points on a line: there is no corner to round, and the centre
  // of any tangent circle would be infinitely far away.
  FemmProblem p;
  const int corner = addCorner(p, -10, 0, 0, 0, 10, 0);

  QVERIFY2(!FemmProblemEdit::createRadius(p, corner, 1.0),
      "a fillet on collinear segments was accepted");
  QCOMPARE(p.arcSegments.size(), 0);
  QCOMPARE(p.nodes.size(), 3);
}

void TestCreateRadius::filletRejectsANonPositiveRadius()
{
  FemmProblem p;
  const int corner = addCorner(p, 0, 10, 0, 0, 10, 0);

  QVERIFY(!FemmProblemEdit::createRadius(p, corner, 0.0));
  QVERIFY(!FemmProblemEdit::createRadius(p, corner, -1.0));
  QCOMPARE(p.arcSegments.size(), 0);
  QCOMPARE(p.nodes.size(), 3);

  // and an out-of-range node index is refused rather than indexed into
  QVERIFY(!FemmProblemEdit::createRadius(p, -1, 1.0));
  QVERIFY(!FemmProblemEdit::createRadius(p, p.nodes.size(), 1.0));
}

void TestCreateRadius::filletRejectsAJunctionOfThreeEdges()
{
  // A T-junction. Which two of the three edges would the fillet be
  // tangent to? There is no answer, so the only correct behaviour is to
  // decline.
  FemmProblem p;
  const int corner = addCorner(p, 0, 10, 0, 0, 10, 0);
  const int third = FemmProblemEdit::addNode(p, -10, 0);
  FemmProblemEdit::addSegment(p, corner, third);

  QVERIFY(!FemmProblemEdit::canCreateRadius(p, corner));
  QVERIFY2(!FemmProblemEdit::createRadius(p, corner, 1.0),
      "a fillet was applied to a node shared by three segments");
  QCOMPARE(p.arcSegments.size(), 0);
  QCOMPARE(p.segments.size(), 3);
}

void TestCreateRadius::filletedCornerSurvivesASaveAndReload()
{
  // The arc is written as two endpoints plus an included angle, so the
  // radius is not stored -- it is reconstructed on load. A rounding or
  // sign error in that round trip would move the arc off its tangent
  // points without changing anything visible in the file.
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  const int corner = addCorner(p, 0, 10, 0, 0, 10, 0);
  const double r = 2.0;
  QVERIFY(FemmProblemEdit::createRadius(p, corner, r));

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("fillet.fem");
  QString error;
  QVERIFY2(FemmFileIO::writeFem(path, p, error), qPrintable(error));

  FemmProblem back;
  QVERIFY2(FemmFileIO::readFem(path, back, error), qPrintable(error));

  QCOMPARE(back.nodes.size(), p.nodes.size());
  QCOMPARE(back.segments.size(), p.segments.size());
  QCOMPARE(back.arcSegments.size(), p.arcSegments.size());

  std::complex<double> centre;
  double radius = 0;
  QVERIFY(FemmProblemEdit::circleFromArc(back, back.arcSegments[0],
      centre, radius));
  QVERIFY2(std::fabs(radius - r) < 1e-6,
      qPrintable(QStringLiteral("after a save/reload the fillet radius is "
                                "%1, was %2").arg(radius).arg(r)));
  QVERIFY(std::fabs(distanceToLine(centre.real(), centre.imag(),
                        0, 0, 0, 10) - r) < 1e-6);
  QVERIFY(std::fabs(distanceToLine(centre.real(), centre.imag(),
                        0, 0, 10, 0) - r) < 1e-6);
}

void TestCreateRadius::boundaryAndGroupAreCarriedOntoTheArc()
{
  // A fillet that silently drops the boundary condition off the edge it
  // replaces part of would leave a gap in the boundary -- and the model
  // would still solve, just wrongly.
  FemmProblem p;
  const int corner = addCorner(p, 0, 10, 0, 0, 10, 0);
  for (FemmSegment& s : p.segments) {
    s.boundaryMarker = 3;
    s.inGroup = 7;
  }

  QVERIFY(FemmProblemEdit::createRadius(p, corner, 2.0));
  QCOMPARE(p.arcSegments.size(), 1);
  QCOMPARE(p.arcSegments[0].boundaryMarker, 3);
  QCOMPARE(p.arcSegments[0].inGroup, 7);

  // and the trimmed legs keep theirs
  for (const FemmSegment& s : p.segments) {
    QCOMPARE(s.boundaryMarker, 3);
    QCOMPARE(s.inGroup, 7);
  }
}

void TestCreateRadius::boundaryChoiceDependsOnCornerWinding()
{
  // Documents a real sharp edge rather than asserting it is fine.
  //
  // When the two legs carry DIFFERENT boundary conditions, the arc
  // inherits from whichever one createRadius ends up calling seg0 -- and
  // seg0/seg1 are swapped when the signed angle between the legs is
  // negative, i.e. depending on which way the corner winds. So the same
  // two edges with the same two markers give the arc a different marker
  // depending on the order they were drawn in.
  //
  // The classic GUI behaves the same way (this mirrors
  // femm/FemmeDoc.cpp's CreateRadius), so this is inherited rather than
  // introduced, and changing it would change behaviour users may rely
  // on. What matters for now is that it is known and pinned: if someone
  // makes the choice deterministic later, this test is where the
  // decision gets recorded.
  auto filletWithMarkers = [](bool reversedWinding) {
    FemmProblem p;
    int corner;
    if (reversedWinding)
      corner = addCorner(p, 10, 0, 0, 0, 0, 10);
    else
      corner = addCorner(p, 0, 10, 0, 0, 10, 0);
    p.segments[0].boundaryMarker = 1;
    p.segments[1].boundaryMarker = 2;
    if (!FemmProblemEdit::createRadius(p, corner, 2.0))
      return -1;
    return p.arcSegments.isEmpty() ? -1 : p.arcSegments[0].boundaryMarker;
  };

  const int forward = filletWithMarkers(false);
  const int reversed = filletWithMarkers(true);
  QVERIFY(forward > 0);
  QVERIFY(reversed > 0);

  // Whichever it picks, it must pick one of the two -- never a marker
  // neither leg had, and never 0 ("no boundary condition"), which would
  // be a silent hole in the boundary.
  QVERIFY2(forward == 1 || forward == 2,
      qPrintable(QStringLiteral("arc got marker %1, neither leg's").arg(forward)));
  QVERIFY2(reversed == 1 || reversed == 2,
      qPrintable(QStringLiteral("arc got marker %1, neither leg's").arg(reversed)));

  if (forward != reversed) {
    qInfo("Create Radius takes the arc's boundary marker from the first "
          "segment in winding order: %d drawing one way, %d the other. "
          "Inherited from the classic GUI; see this test's comment.",
        forward, reversed);
  }
}

// ---------------------------------------------------------------------------
// The sketch layer. Create Radius deletes the corner node, and deleting
// anything renumbers everything above it -- constraints and dimensions
// hold those indices too.
// ---------------------------------------------------------------------------

void TestCreateRadius::filletDoesNotLeaveTheSketchPointingAtTheWrongGeometry()
{
  // A dimension between two nodes ABOVE the corner in index order. The
  // fillet deletes the corner, so both of those nodes shift down one --
  // and before the fix nothing moved the dimension with them, so it went
  // on measuring, silently, between two different points.
  FemmProblem p;
  const int corner = addCorner(p, 0, 10, 0, 0, 10, 0); // nodes 0,1,2; corner = 1
  const int farA = FemmProblemEdit::addNode(p, 100, 0);   // 3
  const int farB = FemmProblemEdit::addNode(p, 100, 50);  // 4

  const double ax = p.nodes[farA].x, ay = p.nodes[farA].y;
  const double bx = p.nodes[farB].x, by = p.nodes[farB].y;

  FemmDimension d;
  d.type = DimensionType::Distance;
  d.refA = farA;
  d.refB = farB;
  d.value = 50.0;
  p.dimensions.push_back(d);

  QVERIFY(FemmProblemEdit::createRadius(p, corner, 2.0));

  QCOMPARE(p.dimensions.size(), 1);
  const FemmDimension& after = p.dimensions[0];
  QVERIFY2(after.refA >= 0 && after.refA < p.nodes.size(),
      "the dimension's first reference is out of range after the fillet");
  QVERIFY2(after.refB >= 0 && after.refB < p.nodes.size(),
      "the dimension's second reference is out of range after the fillet");

  // The test that matters: it must still point at the SAME POINTS, not
  // merely at valid indices.
  QCOMPARE(p.nodes[after.refA].x, ax);
  QCOMPARE(p.nodes[after.refA].y, ay);
  QCOMPARE(p.nodes[after.refB].x, bx);
  QCOMPARE(p.nodes[after.refB].y, by);
}

void TestCreateRadius::filletDropsConstraintsOnTheCornerItRemoved()
{
  // A constraint on the corner node itself cannot survive: the corner is
  // gone, replaced by two tangent points, and there is no defensible
  // answer to "which one did you mean". Dropping it is correct; keeping
  // it pointed at whatever now occupies that index is not.
  FemmProblem p;
  const int corner = addCorner(p, 0, 10, 0, 0, 10, 0);
  const int other = FemmProblemEdit::addNode(p, 50, 50);

  FemmConstraint c;
  c.type = ConstraintType::Coincident;
  c.refA = corner;
  c.refB = other;
  p.constraints.push_back(c);

  QVERIFY(FemmProblemEdit::createRadius(p, corner, 2.0));

  QCOMPARE(p.constraints.size(), 0);
}

void TestCreateRadius::deletingANodeRenumbersDimensionReferences()
{
  // The same defect reached directly, because Create Radius is only one
  // of the ways in. Plain node deletion has it too.
  FemmProblem p;
  FemmProblemEdit::addNode(p, 0, 0);   // 0, about to be deleted
  const int a = FemmProblemEdit::addNode(p, 10, 0);  // 1
  const int b = FemmProblemEdit::addNode(p, 10, 10); // 2

  FemmDimension d;
  d.type = DimensionType::Distance;
  d.refA = a;
  d.refB = b;
  p.dimensions.push_back(d);

  FemmProblemEdit::deleteNode(p, 0);

  QCOMPARE(p.nodes.size(), 2);
  QCOMPARE(p.dimensions.size(), 1);
  QCOMPARE(p.dimensions[0].refA, 0);
  QCOMPARE(p.dimensions[0].refB, 1);
  QCOMPARE(p.nodes[p.dimensions[0].refA].x, 10.0);
  QCOMPARE(p.nodes[p.dimensions[0].refA].y, 0.0);
  QCOMPARE(p.nodes[p.dimensions[0].refB].y, 10.0);
}

void TestCreateRadius::deletingASegmentRenumbersConstraintReferences()
{
  // deleteSegment did no fixing up at all, so a Parallel constraint on
  // two segments above the deleted one silently became a constraint on
  // two different segments.
  FemmProblem p;
  const int n0 = FemmProblemEdit::addNode(p, 0, 0);
  const int n1 = FemmProblemEdit::addNode(p, 10, 0);
  const int n2 = FemmProblemEdit::addNode(p, 0, 10);
  const int n3 = FemmProblemEdit::addNode(p, 10, 10);
  const int n4 = FemmProblemEdit::addNode(p, 0, 20);
  const int n5 = FemmProblemEdit::addNode(p, 10, 20);

  FemmProblemEdit::addSegment(p, n0, n1); // 0, deleted below
  const int s1 = FemmProblemEdit::addSegment(p, n2, n3); // 1
  const int s2 = FemmProblemEdit::addSegment(p, n4, n5); // 2

  FemmConstraint parallel;
  parallel.type = ConstraintType::Parallel;
  parallel.refA = s1;
  parallel.refB = s2;
  p.constraints.push_back(parallel);

  // and one referencing the segment about to go, which must be dropped
  FemmConstraint horizontal;
  horizontal.type = ConstraintType::Horizontal;
  horizontal.refA = 0;
  p.constraints.push_back(horizontal);

  FemmProblemEdit::deleteSegment(p, 0);

  QCOMPARE(p.segments.size(), 2);
  QCOMPARE(p.constraints.size(), 1);
  QCOMPARE(p.constraints[0].type, ConstraintType::Parallel);
  QCOMPARE(p.constraints[0].refA, 0);
  QCOMPARE(p.constraints[0].refB, 1);
  // still the same two segments, by their endpoints
  QCOMPARE(p.nodes[p.segments[p.constraints[0].refA].n0].y, 10.0);
  QCOMPARE(p.nodes[p.segments[p.constraints[0].refB].n0].y, 20.0);
}

QTEST_GUILESS_MAIN(TestCreateRadius)
#include "tst_create_radius.moc"
