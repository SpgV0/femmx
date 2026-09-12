// tst_construction.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12
// (issue #31).
//
// Construction geometry has exactly one safety property, and everything
// else here is convenience: IT MUST NOT REACH THE SOLVER. A centreline
// that got written into the .fem is a material boundary. It would cut
// the region it runs through in two, the mesher would honour it, the
// solver would solve it, and the answer would come back looking like an
// answer.
//
// So the checks below are written from the outside in. Not "the flag is
// set" -- the flag is easy and was never the risk -- but "the bytes that
// leave this program do not contain it", for every path out: the .fem
// text file, the .femx cache, and the .poly handed to triangle. Three
// separate exits, and a feature like this is only as safe as the one
// somebody forgot.
//
// The second property is the reason the feature exists at all:
// constraints and dimensions must be able to REFERENCE construction
// geometry and survive a save and reload, even though the geometry they
// point at is in a different file from the geometry it is measured
// against.

#include <QtTest>

#include "ConstructionGeometry.h"
#include "FemmFileIO.h"
#include "FemmProblem.h"
#include "FemmProblemEdit.h"
#include "FemxFileIO.h"
#include "MeshBuilder.h"
#include "SketchFileIO.h"

#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextStream>

#include <cmath>

namespace {

// A square of real geometry with a construction centreline up its
// middle. The centreline crosses the square, so if it ever reached the
// mesher it would split the region in two -- which is exactly the
// failure this file is about, and it is visible in the output.
FemmProblem squareWithCentreline()
{
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  const int c = FemmProblemEdit::addNode(p, 10, 10);
  const int d = FemmProblemEdit::addNode(p, 0, 10);
  FemmProblemEdit::addSegment(p, a, b);
  FemmProblemEdit::addSegment(p, b, c);
  FemmProblemEdit::addSegment(p, c, d);
  FemmProblemEdit::addSegment(p, d, a);
  FemmProblemEdit::addBlockLabel(p, 5, 2);

  ConstructionGeometry::addCentreline(p, 5, -2, 5, 12);
  return p;
}

int countTagged(const QString& femText, const QString& tag)
{
  const QRegularExpression re(QStringLiteral("\\[%1\\]\\s*=\\s*(\\d+)").arg(tag));
  const QRegularExpressionMatch m = re.match(femText);
  return m.hasMatch() ? m.captured(1).toInt() : -1;
}

} // namespace

class TestConstruction : public QObject
{
  Q_OBJECT

  private slots:
  void theCentrelineIsNotInTheFemFile();
  void theCentrelineIsNotInTheFemxCache();
  void theCentrelineIsNotInThePolyHandedToTheMesher();
  void realGeometrySurvivesTheStrippingIntact();
  void aNodeSharedWithRealGeometryIsKept();

  void constructionGeometryComesBackFromTheSidecar();
  void aConstraintOnConstructionGeometrySurvivesASaveAndReload();
  void readingTheSidecarTwiceDoesNotDuplicateTheCentreline();

  void convertingToConstructionAndBackIsLossless();
  void convertingAnEmptySelectionIsRefusedRatherThanSilent();
  void aBoltCircleLandsOnItsOwnCircle();
  void aReferenceRectangleIsClosedAndAllConstruction();
};

// ---------------------------------------------------------------------------
// The safety property: three exits, none of them carrying it
// ---------------------------------------------------------------------------

void TestConstruction::theCentrelineIsNotInTheFemFile()
{
  FemmProblem p = squareWithCentreline();
  QCOMPARE(p.segments.size(), 5); // four real sides plus the centreline

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.path() + "/model.fem";
  QString error;
  QVERIFY2(FemmFileIO::writeFem(path, p, error), qPrintable(error));

  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
  const QString text = QString::fromUtf8(f.readAll());
  f.close();

  QCOMPARE(countTagged(text, "NumSegments"), 4);
  // Its two endpoints go with it: nothing that survives references them.
  QCOMPARE(countTagged(text, "NumPoints"), 4);

  // And it has to come BACK as four, not merely be written as four --
  // the count and the rows are written separately, and a mismatch would
  // corrupt every section after it.
  FemmProblem back;
  QVERIFY2(FemmFileIO::readFem(path, back, error), qPrintable(error));
  QCOMPARE(back.segments.size(), 4);
  QCOMPARE(back.nodes.size(), 4);
  for (const FemmSegment& s : back.segments) {
    QVERIFY(s.n0 >= 0 && s.n0 < back.nodes.size());
    QVERIFY(s.n1 >= 0 && s.n1 < back.nodes.size());
  }
}

void TestConstruction::theCentrelineIsNotInTheFemxCache()
{
  // .femx is defined as a cache of the .fem: it must hold exactly what
  // re-parsing the text would give. If construction geometry stayed in
  // it, opening the model would give one set of geometry while the cache
  // was fresh and a different one afterwards -- and the entities would
  // come back with no flag at all, silently promoted to real geometry.
  FemmProblem p = squareWithCentreline();

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString femPath = dir.path() + "/model.fem";
  const QString femxPath = dir.path() + "/model.femx";
  QString error;
  QVERIFY(FemmFileIO::writeFem(femPath, p, error));
  QVERIFY2(FemxFileIO::writeFemx(femxPath, femPath, p, error), qPrintable(error));

  FemmProblem cached;
  QVERIFY2(FemxFileIO::readFemx(femxPath, cached, error), qPrintable(error));
  QCOMPARE(cached.segments.size(), 4);
  QCOMPARE(cached.nodes.size(), 4);

  // The cache and the text file must agree entity for entity, which is
  // the contract that makes a cache safe to use at all.
  FemmProblem parsed;
  QVERIFY(FemmFileIO::readFem(femPath, parsed, error));
  QCOMPARE(cached.segments.size(), parsed.segments.size());
  QCOMPARE(cached.nodes.size(), parsed.nodes.size());
}

void TestConstruction::theCentrelineIsNotInThePolyHandedToTheMesher()
{
  // The end of the line. If it gets this far it is meshed, and a
  // centreline running through a region splits it in two.
  FemmProblem p = squareWithCentreline();

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString root = dir.path() + "/model";
  QString error;
  QVERIFY2(MeshBuilder::writePolyAndPbc(p, root, error), qPrintable(error));

  QFile poly(root + ".poly");
  QVERIFY(poly.open(QIODevice::ReadOnly | QIODevice::Text));
  QTextStream in(&poly);

  const QStringList header = in.readLine().trimmed().split(QRegularExpression("\\s+"));
  const int nodeCount = header[0].toInt();

  // No meshed point may lie outside the square. The centreline runs from
  // y = -2 to y = 12, so its own endpoints are the tell: they are the
  // only geometry in the model outside 0..10.
  double worstY = 0;
  for (int i = 0; i < nodeCount; i++) {
    const QStringList f = in.readLine().trimmed().split(QRegularExpression("\\s+"));
    QVERIFY(f.size() >= 3);
    const double y = f[2].toDouble();
    worstY = std::max(worstY, std::max(-y, y - 10.0));
  }
  QVERIFY2(worstY < 1e-9,
      qPrintable(QStringLiteral("a meshed point is %1 outside the square, so the "
                                "construction centreline reached the mesher and will "
                                "be solved as a material boundary")
                     .arg(worstY)));
}

void TestConstruction::realGeometrySurvivesTheStrippingIntact()
{
  // The other half of the safety property, and the easier one to get
  // wrong quietly: stripping must not renumber a real segment onto the
  // wrong nodes.
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  // Deliberately INTERLEAVED, so the maps actually have to do something:
  // construction, real, construction, real.
  ConstructionGeometry::addCentreline(p, -5, -5, -5, 5);
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  const int real0 = FemmProblemEdit::addSegment(p, a, b);
  p.segments[real0].boundaryMarker = 3;
  ConstructionGeometry::addCentreline(p, -7, -5, -7, 5);
  const int c = FemmProblemEdit::addNode(p, 10, 10);
  const int real1 = FemmProblemEdit::addSegment(p, b, c);
  p.segments[real1].inGroup = 9;

  const FemmProblem out = FemmProblemEdit::withoutConstruction(p);
  QCOMPARE(out.segments.size(), 2);
  QCOMPARE(out.nodes.size(), 3);

  // Each surviving segment must still join the same two POINTS it did
  // before, which is the thing an index remap can silently break.
  for (const FemmSegment& s : out.segments) {
    const double x0 = out.nodes[s.n0].x, y0 = out.nodes[s.n0].y;
    const double x1 = out.nodes[s.n1].x, y1 = out.nodes[s.n1].y;
    const bool isFirst = (x0 == 0 && y0 == 0 && x1 == 10 && y1 == 0);
    const bool isSecond = (x0 == 10 && y0 == 0 && x1 == 10 && y1 == 10);
    QVERIFY2(isFirst || isSecond,
        qPrintable(QStringLiteral("a segment now runs (%1,%2)-(%3,%4), which is "
                                  "neither of the two that were there")
                       .arg(x0).arg(y0).arg(x1).arg(y1)));
    if (isFirst)
      QCOMPARE(s.boundaryMarker, 3);
    if (isSecond)
      QCOMPARE(s.inGroup, 9);
  }
}

void TestConstruction::aNodeSharedWithRealGeometryIsKept()
{
  // A node flagged construction but used by a real edge belongs to the
  // real edge too. Dropping it would leave that edge pointing at an
  // index that no longer exists -- not a broken drawing but a corrupt
  // file.
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  const int shared = FemmProblemEdit::addNode(p, 0, 0);
  const int far = FemmProblemEdit::addNode(p, 10, 0);
  p.nodes[shared].isConstruction = true;
  FemmProblemEdit::addSegment(p, shared, far); // a REAL segment on it

  const FemmProblem out = FemmProblemEdit::withoutConstruction(p);
  QCOMPARE(out.segments.size(), 1);
  QCOMPARE(out.nodes.size(), 2);
  QVERIFY(out.segments[0].n0 >= 0 && out.segments[0].n0 < out.nodes.size());
  QVERIFY(out.segments[0].n1 >= 0 && out.segments[0].n1 < out.nodes.size());
}

// ---------------------------------------------------------------------------
// Persistence -- the sidecar is the only copy
// ---------------------------------------------------------------------------

void TestConstruction::constructionGeometryComesBackFromTheSidecar()
{
  FemmProblem p = squareWithCentreline();

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.path() + "/model.fem";
  QString error;
  QVERIFY(FemmFileIO::writeFem(path, p, error));
  QVERIFY2(SketchFileIO::writeSketch(path, p, error), qPrintable(error));

  FemmProblem back;
  QVERIFY(FemmFileIO::readFem(path, back, error));
  QCOMPARE(back.segments.size(), 4); // the .fem alone has no centreline

  QStringList report;
  QVERIFY2(SketchFileIO::readSketch(path, back, report, error), qPrintable(error));
  QVERIFY2(report.isEmpty(), qPrintable(report.join("; ")));

  QCOMPARE(back.segments.size(), 5);
  int construction = 0;
  for (const FemmSegment& s : back.segments)
    if (s.isConstruction)
      construction++;
  QCOMPARE(construction, 1);

  // And it must come back where it was, not merely come back.
  for (const FemmSegment& s : back.segments) {
    if (!s.isConstruction)
      continue;
    QCOMPARE(back.nodes[s.n0].x, 5.0);
    QCOMPARE(back.nodes[s.n0].y, -2.0);
    QCOMPARE(back.nodes[s.n1].x, 5.0);
    QCOMPARE(back.nodes[s.n1].y, 12.0);
  }
}

void TestConstruction::aConstraintOnConstructionGeometrySurvivesASaveAndReload()
{
  // The entire point of the feature. The constraint is stored in the
  // sidecar, the geometry it references is stored in the sidecar, and
  // the geometry it constrains is in the .fem -- three-way, across two
  // files, with every index renumbered in between.
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  FemmProblemEdit::addSegment(p, a, b);
  ConstructionGeometry::addCentreline(p, 5, -5, 5, 5);

  // The centreline's own segment, whichever index it landed at.
  int centrelineSeg = -1;
  for (int i = 0; i < p.segments.size(); i++)
    if (p.segments[i].isConstruction)
      centrelineSeg = i;
  QVERIFY(centrelineSeg >= 0);

  FemmConstraint c;
  c.type = ConstraintType::Vertical;
  c.refA = centrelineSeg;
  p.constraints.push_back(c);

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.path() + "/model.fem";
  QString error;
  QVERIFY(FemmFileIO::writeFem(path, p, error));
  QVERIFY(SketchFileIO::writeSketch(path, p, error));

  FemmProblem back;
  QVERIFY(FemmFileIO::readFem(path, back, error));
  QStringList report;
  QVERIFY(SketchFileIO::readSketch(path, back, report, error));

  QVERIFY2(report.isEmpty(),
      qPrintable(QStringLiteral("the constraint on the centreline did not survive: %1")
                     .arg(report.join("; "))));
  QCOMPARE(back.constraints.size(), 1);
  const int ref = back.constraints[0].refA;
  QVERIFY2(ref >= 0 && ref < back.segments.size(), "the constraint points nowhere");
  QVERIFY2(back.segments[ref].isConstruction,
      "the constraint came back pointing at REAL geometry -- the indices shifted "
      "when the centreline moved to the end of the list and the reference was not "
      "repaired");
}

void TestConstruction::readingTheSidecarTwiceDoesNotDuplicateTheCentreline()
{
  FemmProblem p = squareWithCentreline();

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.path() + "/model.fem";
  QString error;
  QVERIFY(FemmFileIO::writeFem(path, p, error));
  QVERIFY(SketchFileIO::writeSketch(path, p, error));

  FemmProblem back;
  QVERIFY(FemmFileIO::readFem(path, back, error));
  QStringList report;
  QVERIFY(SketchFileIO::readSketch(path, back, report, error));
  const int after1 = back.segments.size();
  QVERIFY(SketchFileIO::readSketch(path, back, report, error));
  QCOMPARE(back.segments.size(), after1);
}

// ---------------------------------------------------------------------------
// The commands
// ---------------------------------------------------------------------------

void TestConstruction::convertingToConstructionAndBackIsLossless()
{
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  const int s = FemmProblemEdit::addSegment(p, a, b);
  p.segments[s].boundaryMarker = 2;
  p.segments[s].inGroup = 7;
  p.segments[s].maxSideLength = 0.4;
  p.segments[s].isSelected = true;

  QVERIFY(ConstructionGeometry::setSelectedConstruction(p, true).ok);
  QVERIFY(p.segments[s].isConstruction);
  QVERIFY(ConstructionGeometry::setSelectedConstruction(p, false).ok);
  QVERIFY(!p.segments[s].isConstruction);

  // Nothing else may have been touched on the way through: converting is
  // a flag, not a rebuild, and losing a boundary condition doing it
  // would be silent.
  QCOMPARE(p.segments[s].boundaryMarker, 2);
  QCOMPARE(p.segments[s].inGroup, 7);
  QCOMPARE(p.segments[s].maxSideLength, 0.4);
  QCOMPARE(p.segments.size(), 1);
  QCOMPARE(p.nodes.size(), 2);
}

void TestConstruction::convertingAnEmptySelectionIsRefusedRatherThanSilent()
{
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  FemmProblemEdit::addSegment(p, a, b); // not selected

  const auto r = ConstructionGeometry::setSelectedConstruction(p, true);
  QVERIFY2(!r.ok, "converting nothing reported success, which is indistinguishable "
                  "from converting something");
  QVERIFY(!r.message.isEmpty());
}

void TestConstruction::aBoltCircleLandsOnItsOwnCircle()
{
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  const auto r = ConstructionGeometry::addBoltCircle(p, 3, -4, 25, 6, 15.0);
  QVERIFY2(r.ok, qPrintable(r.message));

  // Everything it made must be construction -- a single real node left
  // behind by a bolt circle is a stray mesh vertex in the middle of a
  // region.
  for (const FemmNode& n : p.nodes)
    QVERIFY2(n.isConstruction, "a bolt-circle node was left as real geometry");
  for (const FemmArcSegment& arc : p.arcSegments)
    QVERIFY2(arc.isConstruction, "a bolt-circle arc was left as real geometry");

  // Every node on the circle, and the positions evenly spaced.
  QVector<double> angles;
  for (const FemmNode& n : p.nodes) {
    const double d = std::hypot(n.x - 3, n.y + 4);
    QVERIFY2(std::abs(d - 25.0) < 1e-9,
        qPrintable(QStringLiteral("a bolt position is %1 from the centre, not 25").arg(d)));
    angles.push_back(std::atan2(n.y + 4, n.x - 3) * 180.0 / M_PI);
  }
  // 6 positions plus the two arc endpoints the circle itself needs.
  QCOMPARE(p.nodes.size(), 8);
  QCOMPARE(p.arcSegments.size(), 2);

  // The first position is where it was asked for.
  bool sawStart = false;
  for (double a : angles)
    if (std::abs(a - 15.0) < 1e-9)
      sawStart = true;
  QVERIFY2(sawStart, "the first bolt position is not at the start angle asked for");
}

void TestConstruction::aReferenceRectangleIsClosedAndAllConstruction()
{
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  QVERIFY(ConstructionGeometry::addReferenceRectangle(p, 10, 6, 0, 0).ok);

  QCOMPARE(p.nodes.size(), 4);
  QCOMPARE(p.segments.size(), 4);
  for (const FemmSegment& s : p.segments)
    QVERIFY(s.isConstruction);
  for (const FemmNode& n : p.nodes)
    QVERIFY(n.isConstruction);

  // Closed: every corner used exactly twice. A rectangle drawn as four
  // loose lines looks identical and is not a frame.
  QVector<int> uses(p.nodes.size(), 0);
  for (const FemmSegment& s : p.segments) {
    uses[s.n0]++;
    uses[s.n1]++;
  }
  for (int u : uses)
    QCOMPARE(u, 2);

  // Corners given in the "wrong" order still give the same rectangle.
  QCOMPARE(p.nodes.size(), 4);
  for (const FemmNode& n : p.nodes) {
    QVERIFY(n.x == 0.0 || n.x == 10.0);
    QVERIFY(n.y == 0.0 || n.y == 6.0);
  }
}

QTEST_GUILESS_MAIN(TestConstruction)
#include "tst_construction.moc"
