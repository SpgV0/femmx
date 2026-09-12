// tst_arc_geometry.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12
// (issue #25).
//
// Arcs are the one geometry type in the editor with real maths behind
// them. FEMM stores an arc as two endpoints plus an included angle and
// nothing else, so its centre, radius and sweep are all DERIVED -- and
// the rule that makes the derivation unambiguous is that an arc always
// sweeps counterclockwise from n0 to n1. Everything below is ultimately
// a consequence of that rule, including the defect this file found.
//
// #13 covered nodes, segments and labels through the same transforms.
// Arcs behave differently under exactly one of them, which is the point.

#include <QtTest>

#include "DxfIO.h"
#include "FemmFileIO.h"
#include "FemmProblem.h"
#include "FemmProblemEdit.h"

#include <QTemporaryDir>

#include <cmath>
#include <complex>

namespace {

constexpr double kTol = 1e-9;

struct Circle {
  std::complex<double> centre;
  double radius = 0;
};

Circle arcCircle(const FemmProblem& p, int arcIndex)
{
  Circle c;
  const bool ok = FemmProblemEdit::circleFromArc(p, p.arcSegments[arcIndex],
      c.centre, c.radius);
  if (!ok)
    c.radius = -1;
  return c;
}

// A single arc from (x0,y0) to (x1,y1) sweeping `sweepDeg` CCW.
int makeArc(FemmProblem& p, double x0, double y0, double x1, double y1,
    double sweepDeg, double maxSegDeg = 1.0)
{
  const int a = FemmProblemEdit::addNode(p, x0, y0);
  const int b = FemmProblemEdit::addNode(p, x1, y1);
  return FemmProblemEdit::addArcSegment(p, a, b, sweepDeg, maxSegDeg);
}

void selectEverything(FemmProblem& p)
{
  for (FemmNode& n : p.nodes)
    n.isSelected = true;
  for (FemmBlockLabel& b : p.blockLabels)
    b.isSelected = true;
}

} // namespace

class TestArcGeometry : public QObject
{
  Q_OBJECT

private slots:
  void aQuarterArcHasTheCentreAndRadiusItShould();
  void sweepDeterminesWhichSideTheCentreFallsOn();
  void sweepDeterminesWhichSideTheCentreFallsOn_data();

  void arcPropertiesRoundTripThroughAFile();
  void arcGeometryRoundTripsThroughAFile();

  void translateMovesTheCentreAndKeepsTheRadius();
  void rotateMovesTheCentreAndKeepsTheRadius();
  void scaleScalesTheRadius();
  void mirrorReflectsTheArcInsteadOfInvertingIt();
  void mirrorLeavesAPartiallySelectedArcAlone();

  void arcsSurviveDxfExportAndImport();
};

// ---------------------------------------------------------------------------
// The derivation itself
// ---------------------------------------------------------------------------

void TestArcGeometry::aQuarterArcHasTheCentreAndRadiusItShould()
{
  // (1,0) -> (0,1), 90 degrees counterclockwise: the unit circle's first
  // quadrant, so centre (0,0) and radius 1. Nothing in the file says so;
  // it all has to come out of the endpoints and the angle.
  FemmProblem p;
  makeArc(p, 1, 0, 0, 1, 90.0);

  const Circle c = arcCircle(p, 0);
  QVERIFY2(c.radius > 0, "circleFromArc failed on a plain quarter arc");
  QVERIFY(std::fabs(c.radius - 1.0) < kTol);
  QVERIFY(std::fabs(c.centre.real()) < kTol);
  QVERIFY(std::fabs(c.centre.imag()) < kTol);
}

void TestArcGeometry::sweepDeterminesWhichSideTheCentreFallsOn_data()
{
  QTest::addColumn<double>("sweepDeg");
  QTest::addColumn<double>("expectedRadius");
  QTest::addColumn<int>("expectedCentreSide");

  // Chord from (0,-1) to (0,1), length 2, on the y axis, so the centre
  // must lie on the x axis. Which SIDE is the whole question: at exactly
  // 180 degrees it sits on the chord, and it crosses over either side of
  // that. A major arc's centre is on the opposite side from a minor
  // arc's, and getting that wrong silently yields the minor arc instead.
  QTest::newRow("60 deg (minor)") << 60.0 << 2.0 << -1;
  QTest::newRow("90 deg (minor)") << 90.0 << std::sqrt(2.0) << -1;
  QTest::newRow("180 deg (semicircle, on the chord)") << 180.0 << 1.0 << 0;
  QTest::newRow("270 deg (major)") << 270.0 << std::sqrt(2.0) << +1;
  QTest::newRow("300 deg (major)") << 300.0 << 2.0 << +1;
}

void TestArcGeometry::sweepDeterminesWhichSideTheCentreFallsOn()
{
  QFETCH(double, sweepDeg);
  QFETCH(double, expectedRadius);
  QFETCH(int, expectedCentreSide);

  FemmProblem p;
  makeArc(p, 0, -1, 0, 1, sweepDeg);

  const Circle c = arcCircle(p, 0);
  QVERIFY2(c.radius > 0, "circleFromArc failed");
  QVERIFY2(std::fabs(c.radius - expectedRadius) < 1e-9,
      qPrintable(QStringLiteral("sweep %1: radius %2, expected %3")
                     .arg(sweepDeg).arg(c.radius, 0, 'g', 17)
                     .arg(expectedRadius)));
  QVERIFY(std::fabs(c.centre.imag()) < 1e-9);

  const int side = (std::fabs(c.centre.real()) < 1e-9)
      ? 0
      : (c.centre.real() > 0 ? +1 : -1);
  QVERIFY2(side == expectedCentreSide,
      qPrintable(QStringLiteral("sweep %1: centre x is %2 (side %3), expected "
                                "side %4 -- a major arc's centre is on the "
                                "opposite side of the chord from a minor "
                                "one's, so this is the minor arc's circle")
                     .arg(sweepDeg).arg(c.centre.real(), 0, 'g', 17)
                     .arg(side).arg(expectedCentreSide)));

  // The property that actually DEFINES the centre, rather than a number
  // worked out by hand: an arc sweeps counterclockwise from n0 to n1, so
  // rotating n0 about the centre by the included angle must land exactly
  // on n1. Asserting this instead of a precomputed coordinate is what
  // caught the major-arc defect -- a hand-derived expectation can be
  // wrong in the same direction as the code, and a before/after
  // comparison agrees with itself no matter how wrong both halves are.
  const std::complex<double> n0(p.nodes[p.arcSegments[0].n0].x,
      p.nodes[p.arcSegments[0].n0].y);
  const std::complex<double> n1(p.nodes[p.arcSegments[0].n1].x,
      p.nodes[p.arcSegments[0].n1].y);
  const std::complex<double> swept =
      c.centre + std::polar(1.0, sweepDeg * M_PI / 180.0) * (n0 - c.centre);
  QVERIFY2(std::abs(swept - n1) < 1e-9,
      qPrintable(QStringLiteral("sweep %1: rotating n0 about the computed "
                                "centre by the arc's own included angle "
                                "lands at (%2, %3), not on n1 (%4, %5)")
                     .arg(sweepDeg).arg(swept.real()).arg(swept.imag())
                     .arg(n1.real()).arg(n1.imag())));

  // and both endpoints are on the circle
  QVERIFY(std::fabs(std::abs(n0 - c.centre) - c.radius) < 1e-9);
  QVERIFY(std::fabs(std::abs(n1 - c.centre) - c.radius) < 1e-9);
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void TestArcGeometry::arcPropertiesRoundTripThroughAFile()
{
  // maxSideLength is the arc's own meshing control (max degrees per mesh
  // element side) and has no segment equivalent, so nothing else in the
  // suite would notice if it were dropped or confused with the segment
  // field of the same name.
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  const int arc = makeArc(p, 1, 0, 0, 1, 90.0, 2.5);
  p.arcSegments[arc].boundaryMarker = 2;
  p.arcSegments[arc].inGroup = 5;

  FemmBoundaryProp bp;
  bp.name = "outer";
  p.boundaryProps.push_back(bp);
  FemmBoundaryProp bp2;
  bp2.name = "inner";
  p.boundaryProps.push_back(bp2);

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("arc.fem");
  QString error;
  QVERIFY2(FemmFileIO::writeFem(path, p, error), qPrintable(error));

  FemmProblem back;
  QVERIFY2(FemmFileIO::readFem(path, back, error), qPrintable(error));
  QCOMPARE(back.arcSegments.size(), 1);
  QCOMPARE(back.arcSegments[0].arcLength, 90.0);
  QCOMPARE(back.arcSegments[0].maxSideLength, 2.5);
  QCOMPARE(back.arcSegments[0].boundaryMarker, 2);
  QCOMPARE(back.arcSegments[0].inGroup, 5);
}

void TestArcGeometry::arcGeometryRoundTripsThroughAFile()
{
  // The radius is not stored, so it has to survive being thrown away and
  // recomputed. A major arc is used deliberately: it is the case where a
  // sign convention error shows up as a centre on the wrong side rather
  // than as a slightly wrong number.
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  makeArc(p, 0, -1, 0, 1, 270.0);
  const Circle before = arcCircle(p, 0);

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("major_arc.fem");
  QString error;
  QVERIFY2(FemmFileIO::writeFem(path, p, error), qPrintable(error));

  FemmProblem back;
  QVERIFY2(FemmFileIO::readFem(path, back, error), qPrintable(error));
  const Circle after = arcCircle(back, 0);

  QVERIFY(std::fabs(after.radius - before.radius) < 1e-6);
  QVERIFY(std::fabs(after.centre.real() - before.centre.real()) < 1e-6);
  QVERIFY(std::fabs(after.centre.imag() - before.centre.imag()) < 1e-6);
}

// ---------------------------------------------------------------------------
// Transforms
// ---------------------------------------------------------------------------

void TestArcGeometry::translateMovesTheCentreAndKeepsTheRadius()
{
  FemmProblem p;
  makeArc(p, 1, 0, 0, 1, 90.0);
  const Circle before = arcCircle(p, 0);

  selectEverything(p);
  FemmProblemEdit::moveSelected(p, 10.0, -4.0);

  const Circle after = arcCircle(p, 0);
  QVERIFY(std::fabs(after.radius - before.radius) < kTol);
  QVERIFY(std::fabs(after.centre.real() - (before.centre.real() + 10.0)) < kTol);
  QVERIFY(std::fabs(after.centre.imag() - (before.centre.imag() - 4.0)) < kTol);
  QCOMPARE(p.arcSegments[0].arcLength, 90.0);
}

void TestArcGeometry::rotateMovesTheCentreAndKeepsTheRadius()
{
  // Rotation preserves handedness, so the sweep must NOT change -- the
  // counterpart to the mirror case below.
  FemmProblem p;
  makeArc(p, 2, 0, 0, 2, 90.0);
  const Circle before = arcCircle(p, 0);

  selectEverything(p);
  FemmProblemEdit::rotateSelected(p, 0.0, 0.0, 90.0);

  const Circle after = arcCircle(p, 0);
  QVERIFY(std::fabs(after.radius - before.radius) < kTol);
  // centred on the origin, so rotating about the origin moves it nowhere
  QVERIFY(std::fabs(after.centre.real() - before.centre.real()) < kTol);
  QVERIFY(std::fabs(after.centre.imag() - before.centre.imag()) < kTol);
  QCOMPARE(p.arcSegments[0].arcLength, 90.0);

  // and the endpoints did move
  QVERIFY(std::fabs(p.nodes[0].x - 0.0) < kTol);
  QVERIFY(std::fabs(p.nodes[0].y - 2.0) < kTol);
}

void TestArcGeometry::scaleScalesTheRadius()
{
  FemmProblem p;
  makeArc(p, 1, 0, 0, 1, 90.0);

  selectEverything(p);
  FemmProblemEdit::scaleSelected(p, 0.0, 0.0, 3.0);

  const Circle after = arcCircle(p, 0);
  QVERIFY2(std::fabs(after.radius - 3.0) < kTol,
      qPrintable(QStringLiteral("radius %1 after scaling a unit arc by 3")
                     .arg(after.radius, 0, 'g', 17)));
  QCOMPARE(p.arcSegments[0].arcLength, 90.0); // angles are scale-invariant
}

void TestArcGeometry::mirrorReflectsTheArcInsteadOfInvertingIt()
{
  // THE DEFECT THIS FILE FOUND. An arc sweeps counterclockwise from n0 to
  // n1; reflection reverses handedness, so reflecting the endpoints alone
  // leaves that rule describing the mirror image's complement -- a
  // different arc, not a mirrored one.
  //
  // Measured before the fix: this exact case came back with its centre at
  // (1,-1) instead of the origin. Nothing complained.
  FemmProblem p;
  makeArc(p, 1, 0, 0, 1, 90.0);
  const Circle before = arcCircle(p, 0);
  QVERIFY(std::fabs(before.centre.real()) < kTol);
  QVERIFY(std::fabs(before.centre.imag()) < kTol);

  selectEverything(p);
  FemmProblemEdit::mirrorSelected(p, 0, 0, 1, 0); // about the x axis

  const Circle after = arcCircle(p, 0);
  QVERIFY2(std::fabs(after.radius - before.radius) < kTol,
      qPrintable(QStringLiteral("mirroring changed the radius: %1 -> %2")
                     .arg(before.radius).arg(after.radius)));
  QVERIFY2(std::fabs(after.centre.real() - before.centre.real()) < kTol
          && std::fabs(after.centre.imag() + before.centre.imag()) < kTol,
      qPrintable(QStringLiteral("a unit arc centred on the origin, mirrored "
                                "about the x axis, should still be centred "
                                "on the origin -- its centre is (%1, %2). "
                                "The arc was inverted rather than mirrored.")
                     .arg(after.centre.real(), 0, 'g', 17)
                     .arg(after.centre.imag(), 0, 'g', 17)));

  // The endpoints reflected, and the sweep is unchanged in magnitude --
  // the handedness is carried by the endpoint ORDER, not by the angle.
  QCOMPARE(p.arcSegments[0].arcLength, 90.0);
  const FemmNode& n0 = p.nodes[p.arcSegments[0].n0];
  const FemmNode& n1 = p.nodes[p.arcSegments[0].n1];
  QVERIFY(std::fabs(n0.x - 0.0) < kTol && std::fabs(n0.y + 1.0) < kTol);
  QVERIFY(std::fabs(n1.x - 1.0) < kTol && std::fabs(n1.y - 0.0) < kTol);
}

void TestArcGeometry::mirrorLeavesAPartiallySelectedArcAlone()
{
  // One endpoint in the selection and one outside means the arc was
  // stretched across the mirror line, not mirrored. There is no
  // orientation fix that makes that meaningful, so the endpoint order
  // must be left as it is rather than swapped on a half-transformed arc.
  FemmProblem p;
  makeArc(p, 1, 0, 0, 1, 90.0);
  const int n0Before = p.arcSegments[0].n0;
  const int n1Before = p.arcSegments[0].n1;

  p.nodes[0].isSelected = true; // only one end
  FemmProblemEdit::mirrorSelected(p, 0, 0, 1, 0);

  QCOMPARE(p.arcSegments[0].n0, n0Before);
  QCOMPARE(p.arcSegments[0].n1, n1Before);
}

// ---------------------------------------------------------------------------
// DXF
// ---------------------------------------------------------------------------

void TestArcGeometry::arcsSurviveDxfExportAndImport()
{
  // An arc is the entity most likely to be flattened into line segments
  // or dropped by an exchange format, and a silently flattened arc still
  // looks curved on screen at the zoom level it was drawn at.
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  makeArc(p, 1, 0, 0, 1, 90.0);
  const Circle before = arcCircle(p, 0);

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("arc.dxf");
  QString error;
  QVERIFY2(DxfIO::exportDxf(path, p, error), qPrintable(error));

  FemmProblem back;
  double suggestedTolerance = 0;
  QVERIFY2(DxfIO::parseDxf(path, back, suggestedTolerance, error),
      qPrintable(error));

  QVERIFY2(!back.arcSegments.isEmpty(),
      qPrintable(QStringLiteral("the arc did not survive the DXF round trip: "
                                "%1 arcs, %2 segments came back")
                     .arg(back.arcSegments.size()).arg(back.segments.size())));

  const Circle after = arcCircle(back, 0);
  QVERIFY2(std::fabs(after.radius - before.radius) < 1e-6,
      qPrintable(QStringLiteral("radius %1 through DXF, was %2")
                     .arg(after.radius).arg(before.radius)));
  QVERIFY(std::fabs(after.centre.real() - before.centre.real()) < 1e-6);
  QVERIFY(std::fabs(after.centre.imag() - before.centre.imag()) < 1e-6);
}

QTEST_GUILESS_MAIN(TestArcGeometry)
#include "tst_arc_geometry.moc"
