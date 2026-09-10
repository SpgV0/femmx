// tst_femmqt_core.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-10.
//
// The first automated test binary femmqt has ever had (issue #11). Before
// this there was no enable_testing(), no add_test() and no Qt6::Test
// anywhere in the tree: CI compiled femmqt and stopped, and the only
// self-check in existence was the ad-hoc --test-constraints mode wired
// into main.cpp, which nothing ran automatically.
//
// This file's job is to prove the harness works end to end -- that the
// app's own code can be linked into a QTest binary, that it runs
// offscreen with no desktop session, and that ctest reports it. The
// substantial per-area suites (the constraint/dimension layer,
// GeometryScene editing, golden-image rendering, mesh generation,
// post-processing parity) are separate tickets that build on this one.
//
// Everything here is deliberately GUI-free: these types have no widget
// dependency, which is exactly why splitting femmqt into a core library
// made them reachable.

#include <QtTest>

#include "DxfIO.h"
#include "FemmFileIO.h"
#include "FemmProblem.h"
#include "FemmProblemEdit.h"

#include <QTemporaryDir>

class TestFemmqtCore : public QObject
{
  Q_OBJECT

private slots:
  // --- the harness itself -------------------------------------------------

  void offscreenPlatformIsInUse();

  // --- FemmProblem / file IO ---------------------------------------------

  void emptyProblemHasSaneDefaults();
  void femRoundTripPreservesGeometry();
  void readFemRejectsAMissingFile();

  // --- geometry editing ---------------------------------------------------

  void addNodeAndSegmentBuildAGraph();
  void splitIntersectingSegmentsAddsTheCrossingNode();

  // --- DXF ----------------------------------------------------------------

  void parseDxfRejectsGarbage();
};

// ---------------------------------------------------------------------------

void TestFemmqtCore::offscreenPlatformIsInUse()
{
  // The CMake harness sets QT_QPA_PLATFORM=offscreen for this target. If
  // that ever stops happening, a test that touches a widget will try to
  // open a real window and either fail or hang on a headless runner --
  // far more confusing than this assertion failing first.
  QCOMPARE(qgetenv("QT_QPA_PLATFORM"), QByteArray("offscreen"));
}

void TestFemmqtCore::emptyProblemHasSaneDefaults()
{
  FemmProblem p;
  QVERIFY(p.nodes.isEmpty());
  QVERIFY(p.segments.isEmpty());
  QVERIFY(p.arcSegments.isEmpty());
  QVERIFY(p.blockLabels.isEmpty());
  QVERIFY(p.precision > 0.0);
  QVERIFY(p.minAngle > 0.0);
}

void TestFemmqtCore::femRoundTripPreservesGeometry()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("roundtrip.fem");

  FemmProblem written;
  written.problemType = FemmCoordinateType::Planar;
  written.depth = 12.5;
  written.frequency = 0.0;
  for (const QPointF pt : { QPointF(0, 0), QPointF(10, 0),
                            QPointF(10, 6), QPointF(0, 6) }) {
    FemmNode n;
    n.x = pt.x();
    n.y = pt.y();
    written.nodes.push_back(n);
  }
  for (int i = 0; i < 4; ++i) {
    FemmSegment s;
    s.n0 = i;
    s.n1 = (i + 1) % 4;
    written.segments.push_back(s);
  }

  QString error;
  QVERIFY2(FemmFileIO::writeFem(path, written, error), qPrintable(error));

  FemmProblem read;
  QVERIFY2(FemmFileIO::readFem(path, read, error), qPrintable(error));

  QCOMPARE((int)read.nodes.size(), (int)written.nodes.size());
  QCOMPARE((int)read.segments.size(), (int)written.segments.size());
  QCOMPARE(read.problemType, written.problemType);
  QCOMPARE(read.depth, written.depth);
  for (int i = 0; i < written.nodes.size(); ++i) {
    QVERIFY(qFuzzyCompare(1.0 + read.nodes[i].x, 1.0 + written.nodes[i].x));
    QVERIFY(qFuzzyCompare(1.0 + read.nodes[i].y, 1.0 + written.nodes[i].y));
  }
}

void TestFemmqtCore::readFemRejectsAMissingFile()
{
  FemmProblem p;
  QString error;
  QVERIFY(!FemmFileIO::readFem("no/such/file/anywhere.fem", p, error));
  QVERIFY2(!error.isEmpty(), "a failed read must say why");
}

void TestFemmqtCore::addNodeAndSegmentBuildAGraph()
{
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 0.0, 0.0);
  const int b = FemmProblemEdit::addNode(p, 5.0, 0.0);
  QCOMPARE((int)p.nodes.size(), 2);
  QVERIFY(a != b);

  // addSegment returns the segment INDEX, and 0 is a perfectly good one,
  // so this checks the container and the returned index rather than the
  // truthiness of the return.
  QCOMPARE(FemmProblemEdit::addSegment(p, a, b), 0);
  QCOMPARE((int)p.segments.size(), 1);
  QCOMPARE(p.segments[0].n0, a);
  QCOMPARE(p.segments[0].n1, b);

  // addSegment is an unconditional append: it is a primitive, and
  // de-duplication is not its job. Pinned because the first draft of this
  // test assumed the opposite -- if a caller ever starts relying on the
  // primitive to reject duplicates, that decision should be visible here
  // rather than silently assumed.
  QCOMPARE(FemmProblemEdit::addSegment(p, a, b), 1);
  QCOMPARE((int)p.segments.size(), 2);
}

void TestFemmqtCore::splitIntersectingSegmentsAddsTheCrossingNode()
{
  // An X: two segments that cross without sharing an endpoint. Classic
  // FEMM splits these in FancyEnforcePSLG; femmqt grew the equivalent in
  // v2.2.x. Without the split the mesher sees no shared vertex at the
  // crossing, so the regions the lines appear to bound are not bounded.
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, -5.0, -5.0);
  const int b = FemmProblemEdit::addNode(p, 5.0, 5.0);
  const int c = FemmProblemEdit::addNode(p, -5.0, 5.0);
  const int d = FemmProblemEdit::addNode(p, 5.0, -5.0);
  FemmProblemEdit::addSegment(p, a, b);
  FemmProblemEdit::addSegment(p, c, d);
  QCOMPARE((int)p.segments.size(), 2);

  FemmProblemEdit::splitIntersectingSegments(p);

  QCOMPARE((int)p.segments.size(), 4);
  QCOMPARE((int)p.nodes.size(), 5);

  bool foundOrigin = false;
  for (const FemmNode& n : p.nodes) {
    if (qAbs(n.x) < 1e-9 && qAbs(n.y) < 1e-9)
      foundOrigin = true;
  }
  QVERIFY2(foundOrigin, "no node was inserted at the crossing point");
}

void TestFemmqtCore::parseDxfRejectsGarbage()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("garbage.dxf");
  {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("this is not a dxf file at all\n");
  }

  FemmProblem parsed;
  double tolerance = 0.0;
  QString error;
  QVERIFY(!DxfIO::parseDxf(path, parsed, tolerance, error));
  QVERIFY2(!error.isEmpty(), "a rejected DXF must say why");
}

// QTEST_GUILESS_MAIN, not QTEST_MAIN: everything in this file is GUI-free,
// and a QCoreApplication needs no QPA platform plugin at all. QTEST_MAIN
// builds a QApplication, which does -- and windeployqt deploys only
// qwindows.dll by default, so with QT_QPA_PLATFORM=offscreen the binary sat
// there unable to load qoffscreen.dll and ctest killed it at its 300s
// timeout with no output whatsoever. The CMake harness now also copies the
// offscreen plugin next to this binary, so a future widget test can switch
// to QTEST_MAIN and still run headless.
QTEST_GUILESS_MAIN(TestFemmqtCore)
#include "tst_femmqt_core.moc"
