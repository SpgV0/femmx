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
#include "MeshBuilder.h"

#include <QDir>
#include <QRegularExpression>
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

  void theMesherDiscretisesAMajorArcAlongTheRightCircle();
  void noFileKeepsItsOwnCopyOfTheArcCentreFormula();
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

// ---------------------------------------------------------------------------
// The fix that only reached one of four places (issue #77)
// ---------------------------------------------------------------------------
//
// #25 fixed the major-arc centre in FemmProblemEdit::circleFromArc. The
// mesher, the canvas and the solution viewer each had their OWN copy of
// the same formula, still carrying the sign error, so an arc of more
// than 180 degrees went on being meshed and drawn against the minor
// arc's circle after #25 was closed.
//
// Nothing above this point could have caught that, because every case
// above goes through the shared function -- which was correct. The two
// checks below cover the gap from both ends: one measures the MESHER's
// actual output, and one asserts that the formula exists in exactly one
// place, so the next copy cannot quietly reintroduce it.

void TestArcGeometry::theMesherDiscretisesAMajorArcAlongTheRightCircle()
{
  // A 270-degree arc from (0,-1) to (0,1). Its true centre is (1,0);
  // the broken formula puts it at (-1,0) -- a mirror image, same radius,
  // which is why nothing downstream noticed.
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  const int n0 = FemmProblemEdit::addNode(p, 0, -1);
  const int n1 = FemmProblemEdit::addNode(p, 0, 1);
  FemmProblemEdit::addArcSegment(p, n0, n1, 270.0, 5.0);

  // The expected circle is derived HERE, from the definition, and
  // deliberately NOT from circleFromArc.
  //
  // Written the obvious way -- ask circleFromArc for the centre and
  // check the mesher's points against it -- this test passed while the
  // defect was reintroduced, because the mesher and the expectation were
  // then reading the same broken function and agreed with each other.
  // That is #25's own trap verbatim, and this file's header warns about
  // it.
  //
  // The definition: an arc sweeps counterclockwise from n0 to n1 through
  // its included angle. The radius has no sign ambiguity, and of the two
  // points at that radius from both endpoints, the centre is whichever
  // one rotating n0 about it by the included angle carries to n1.
  const std::complex<double> a(0, -1), b(0, 1);
  const double sweepRad = 270.0 * M_PI / 180.0;
  const double R = std::abs(b - a) / (2.0 * std::sin(sweepRad / 2.0));
  const std::complex<double> mid = 0.5 * (a + b);
  const std::complex<double> perp =
      std::complex<double>(0, 1) * (b - a) / std::abs(b - a);
  const double half = std::sqrt(std::max(0.0, R * R - std::norm(b - a) / 4.0));
  std::complex<double> centre;
  bool foundCentre = false;
  for (const std::complex<double>& cand : { mid + half * perp, mid - half * perp }) {
    const std::complex<double> swept =
        cand + std::polar(1.0, sweepRad) * (a - cand);
    if (std::abs(swept - b) < 1e-9) {
      centre = cand;
      foundCentre = true;
    }
  }
  QVERIFY2(foundCentre, "the test could not derive the arc's centre from its "
                        "own definition, so it cannot judge the mesher");

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString root = dir.path() + "/majorarc";
  QString error;
  QVERIFY2(MeshBuilder::writePolyAndPbc(p, root, error), qPrintable(error));

  QFile poly(root + ".poly");
  QVERIFY(poly.open(QIODevice::ReadOnly | QIODevice::Text));
  QTextStream in(&poly);
  const QStringList header = in.readLine().trimmed().split(QRegularExpression("\\s+"));
  QVERIFY(header.size() >= 1);
  const int count = header[0].toInt();
  QVERIFY2(count > 2, "the arc was not discretised into intermediate points at all, "
                      "so this cannot tell which circle it followed");

  // Every point the mesher emitted must sit on the arc's real circle.
  // Under the old formula they sit on the mirrored one instead, so the
  // measured radius comes out around 2*|centre| off.
  double worst = 0;
  for (int i = 0; i < count; i++) {
    const QStringList f = in.readLine().trimmed().split(QRegularExpression("\\s+"));
    QVERIFY(f.size() >= 3);
    const std::complex<double> pt(f[1].toDouble(), f[2].toDouble());
    worst = std::max(worst, std::fabs(std::abs(pt - centre) - R));
  }
  QVERIFY2(worst < 1e-6,
      qPrintable(QStringLiteral("a meshed point is %1 away from the arc's own circle "
                                "(centre %2,%3 radius %4) -- the mesher followed a "
                                "different circle than the one the arc describes, so "
                                "the region boundary handed to the solver is not the "
                                "one that was drawn")
                     .arg(worst).arg(centre.real()).arg(centre.imag()).arg(R)));
}

void TestArcGeometry::noFileKeepsItsOwnCopyOfTheArcCentreFormula()
{
  // The behavioural check above covers the mesher. It cannot cover the
  // canvas or the solution viewer without a running GUI, and it cannot
  // cover the copy somebody adds next week at all.
  //
  // So: the expression itself is the thing being guarded. It belongs in
  // FemmProblemEdit.cpp and nowhere else. A text scan is a blunt
  // instrument, but this defect was a blunt one -- the same twelve
  // characters, pasted into three files, each of which then missed a
  // fix applied to the original.
  QDir dir(QStringLiteral(FEMMQT_SOURCE_DIR));
  const QStringList sources = dir.entryList(QStringList() << "*.cpp", QDir::Files);
  QVERIFY2(!sources.isEmpty(), "found no sources to scan");

  QStringList offenders;
  for (const QString& name : sources) {
    QFile f(dir.filePath(name));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
      continue;
    const QString text = QString::fromUtf8(f.readAll());
    // The broken form specifically: a positive square root of
    // R*R - d*d/4, which is |R cos(theta/2)| and drops the sign.
    if (text.contains(QRegularExpression("sqrt\\s*\\(\\s*std::max\\s*\\(\\s*0(\\.0)?\\s*,\\s*R \\* R - d \\* d / 4")))
      offenders << name;
  }
  QVERIFY2(offenders.isEmpty(),
      qPrintable(QStringLiteral("these files compute an arc centre with the "
                                "always-positive square root that #25 fixed, so major "
                                "arcs get the minor arc's circle there: %1. Call "
                                "FemmProblemEdit::circleFromArcPoints instead.")
                     .arg(offenders.join(", "))));
}

QTEST_GUILESS_MAIN(TestArcGeometry)
#include "tst_arc_geometry.moc"
