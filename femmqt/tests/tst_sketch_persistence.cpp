// tst_sketch_persistence.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12
// (issue #27).
//
// The sketch layer used to be session-only: close the file and every
// constraint, every dimension and all the parametric intent behind the
// geometry was gone, leaving coordinates. The tests below are organised
// around the two things that make persistence safe rather than merely
// present:
//
//   * a .fem written by femmqt stays readable by the classic editor and
//     all four solvers, which know nothing about constraints -- so the
//     sketch lives in a sidecar and nothing about the .fem changes;
//
//   * references are stored with a geometric fingerprint, because an
//     index alone means nothing across sessions, and anything that
//     cannot be reattached is REPORTED rather than applied to whatever
//     now occupies its index. That last part is the whole safety
//     argument, so most of this file is about it.

#include <QtTest>

#include "ConstraintSolver.h"
#include "FemmFileIO.h"
#include "FemmProblem.h"
#include "FemmProblemEdit.h"
#include "SketchFileIO.h"

#include <QFile>
#include <QTemporaryDir>

#include <cmath>

namespace {

// A rectangle with a Horizontal constraint on its bottom edge and a
// Distance dimension across it.
FemmProblem sketchedRectangle()
{
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  const int n0 = FemmProblemEdit::addNode(p, 0, 0);
  const int n1 = FemmProblemEdit::addNode(p, 10, 0);
  const int n2 = FemmProblemEdit::addNode(p, 10, 6);
  const int n3 = FemmProblemEdit::addNode(p, 0, 6);
  FemmProblemEdit::addSegment(p, n0, n1);
  FemmProblemEdit::addSegment(p, n1, n2);
  FemmProblemEdit::addSegment(p, n2, n3);
  FemmProblemEdit::addSegment(p, n3, n0);

  FemmConstraint h;
  h.type = ConstraintType::Horizontal;
  h.refA = 0;
  p.constraints.push_back(h);

  FemmConstraint v;
  v.type = ConstraintType::Vertical;
  v.refA = 1;
  p.constraints.push_back(v);

  FemmDimension d;
  d.type = DimensionType::Distance;
  d.refA = n0;
  d.refB = n1;
  d.value = 10.0;
  d.labelOffsetX = 0.5;
  d.labelOffsetY = -2.0;
  p.dimensions.push_back(d);

  return p;
}

} // namespace

class TestSketchPersistence : public QObject
{
  Q_OBJECT

private slots:
  void sidecarSitsBesideTheModel();
  void constraintsAndDimensionsSurviveARoundTrip();
  void theSketchSolvesTheSameAfterReload();

  void aModelWithNoSidecarLoadsAsAnUnconstrainedSketch();
  void theFemItselfIsUnchangedByHavingASketch();
  void savingAnEmptySketchRemovesAStaleSidecar();

  void aMovedReferenceIsFoundByFingerprint();
  void aDeletedReferenceIsReportedNotReattached();
  void anAmbiguousReferenceIsReportedRatherThanGuessed();
  void aNewerFormatIsRefusedRatherThanMisread();
};

// ---------------------------------------------------------------------------

void TestSketchPersistence::sidecarSitsBesideTheModel()
{
  QCOMPARE(SketchFileIO::sidecarPathFor("C:/models/coil.fem"),
      QStringLiteral("C:/models/coil.fes"));
  // completeBaseName, so a dotted name keeps all of it
  QCOMPARE(SketchFileIO::sidecarPathFor("C:/models/coil.v2.fem"),
      QStringLiteral("C:/models/coil.v2.fes"));
  QCOMPARE(SketchFileIO::sidecarPathFor(QString()), QString());
}

void TestSketchPersistence::constraintsAndDimensionsSurviveARoundTrip()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString fem = dir.filePath("rect.fem");

  FemmProblem p = sketchedRectangle();
  QString error;
  QVERIFY2(FemmFileIO::writeFem(fem, p, error), qPrintable(error));
  QVERIFY2(SketchFileIO::writeSketch(fem, p, error), qPrintable(error));
  QVERIFY(QFile::exists(SketchFileIO::sidecarPathFor(fem)));

  FemmProblem back;
  QVERIFY2(FemmFileIO::readFem(fem, back, error), qPrintable(error));
  QStringList report;
  QVERIFY2(SketchFileIO::readSketch(fem, back, report, error), qPrintable(error));
  QVERIFY2(report.isEmpty(), qPrintable(report.join("; ")));

  QCOMPARE(back.constraints.size(), p.constraints.size());
  QCOMPARE(back.dimensions.size(), p.dimensions.size());

  for (int i = 0; i < p.constraints.size(); i++) {
    QCOMPARE((int)back.constraints[i].type, (int)p.constraints[i].type);
    QCOMPARE(back.constraints[i].refA, p.constraints[i].refA);
    QCOMPARE(back.constraints[i].refB, p.constraints[i].refB);
    QCOMPARE(back.constraints[i].refC, p.constraints[i].refC);
  }
  const FemmDimension& d = back.dimensions[0];
  QCOMPARE((int)d.type, (int)DimensionType::Distance);
  QCOMPARE(d.refA, p.dimensions[0].refA);
  QCOMPARE(d.refB, p.dimensions[0].refB);
  QCOMPARE(d.value, 10.0);
  // the label's placement is part of the drawing, not incidental
  QCOMPARE(d.labelOffsetX, 0.5);
  QCOMPARE(d.labelOffsetY, -2.0);
}

void TestSketchPersistence::theSketchSolvesTheSameAfterReload()
{
  // The round trip is only worth anything if the restored sketch DRIVES
  // the geometry the same way. Comparing the solver's own output rather
  // than the stored fields catches a reference that survived as a number
  // but now points somewhere else.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString fem = dir.filePath("solve.fem");

  FemmProblem p = sketchedRectangle();
  // nudge a node so the constraints have work to do on both runs
  p.nodes[1].y = 0.75;

  QString error;
  QVERIFY2(FemmFileIO::writeFem(fem, p, error), qPrintable(error));
  QVERIFY2(SketchFileIO::writeSketch(fem, p, error), qPrintable(error));

  FemmProblem a = p;
  const ConstraintSolver::SolveResult ra = ConstraintSolver::solve(a);

  FemmProblem b;
  QVERIFY2(FemmFileIO::readFem(fem, b, error), qPrintable(error));
  QStringList report;
  QVERIFY2(SketchFileIO::readSketch(fem, b, report, error), qPrintable(error));
  QVERIFY(report.isEmpty());
  const ConstraintSolver::SolveResult rb = ConstraintSolver::solve(b);

  QCOMPARE(rb.converged, ra.converged);
  QCOMPARE(b.nodes.size(), a.nodes.size());
  for (int i = 0; i < a.nodes.size(); i++) {
    QVERIFY2(std::fabs(a.nodes[i].x - b.nodes[i].x) < 1e-9
            && std::fabs(a.nodes[i].y - b.nodes[i].y) < 1e-9,
        qPrintable(QStringLiteral("node %1 solved to (%2, %3) before the "
                                  "round trip and (%4, %5) after")
                       .arg(i).arg(a.nodes[i].x).arg(a.nodes[i].y)
                       .arg(b.nodes[i].x).arg(b.nodes[i].y)));
  }
  // and the health classification matches, which is what the canvas
  // colours nodes by
  QCOMPARE(rb.nodeStatus.size(), ra.nodeStatus.size());
  for (auto it = ra.nodeStatus.constBegin(); it != ra.nodeStatus.constEnd(); ++it) {
    QVERIFY(rb.nodeStatus.contains(it.key()));
    QCOMPARE((int)rb.nodeStatus.value(it.key()), (int)it.value());
  }
}

// ---------------------------------------------------------------------------
// Degrading gracefully in both directions
// ---------------------------------------------------------------------------

void TestSketchPersistence::aModelWithNoSidecarLoadsAsAnUnconstrainedSketch()
{
  // Every model that predates this feature is in exactly this state, so
  // a missing sidecar must be success, not an error.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString fem = dir.filePath("plain.fem");

  FemmProblem p = sketchedRectangle();
  p.constraints.clear();
  p.dimensions.clear();
  QString error;
  QVERIFY2(FemmFileIO::writeFem(fem, p, error), qPrintable(error));
  QVERIFY(!QFile::exists(SketchFileIO::sidecarPathFor(fem)));

  FemmProblem back;
  QVERIFY2(FemmFileIO::readFem(fem, back, error), qPrintable(error));
  QStringList report;
  QVERIFY2(SketchFileIO::readSketch(fem, back, report, error),
      qPrintable(error));
  QVERIFY(report.isEmpty());
  QCOMPARE(back.constraints.size(), 0);
  QCOMPARE(back.dimensions.size(), 0);
}

void TestSketchPersistence::theFemItselfIsUnchangedByHavingASketch()
{
  // The hard requirement: a .fem written by femmqt must stay readable by
  // the classic MFC editor and by all four solvers, none of which know
  // anything about constraints. The cheapest way to guarantee that is
  // for the sketch to change nothing about the .fem at all -- so this
  // compares the bytes.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString withSketch = dir.filePath("with.fem");
  const QString without = dir.filePath("without.fem");

  FemmProblem p = sketchedRectangle();
  FemmProblem bare = p;
  bare.constraints.clear();
  bare.dimensions.clear();

  QString error;
  QVERIFY2(FemmFileIO::writeFem(withSketch, p, error), qPrintable(error));
  QVERIFY2(SketchFileIO::writeSketch(withSketch, p, error), qPrintable(error));
  QVERIFY2(FemmFileIO::writeFem(without, bare, error), qPrintable(error));

  QFile a(withSketch), b(without);
  QVERIFY(a.open(QIODevice::ReadOnly));
  QVERIFY(b.open(QIODevice::ReadOnly));
  QCOMPARE(a.readAll(), b.readAll());
}

void TestSketchPersistence::savingAnEmptySketchRemovesAStaleSidecar()
{
  // Deleting the last constraint has to persist. Leaving the old sidecar
  // would resurrect the sketch on the next open, which is worse than
  // never having saved it.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString fem = dir.filePath("emptied.fem");

  FemmProblem p = sketchedRectangle();
  QString error;
  QVERIFY2(FemmFileIO::writeFem(fem, p, error), qPrintable(error));
  QVERIFY2(SketchFileIO::writeSketch(fem, p, error), qPrintable(error));
  QVERIFY(QFile::exists(SketchFileIO::sidecarPathFor(fem)));

  p.constraints.clear();
  p.dimensions.clear();
  QVERIFY2(SketchFileIO::writeSketch(fem, p, error), qPrintable(error));
  QVERIFY2(!QFile::exists(SketchFileIO::sidecarPathFor(fem)),
      "the sidecar survived the sketch being emptied, so the next open "
      "would bring the deleted constraints back");
}

// ---------------------------------------------------------------------------
// Reference resolution -- the part that makes this safe
// ---------------------------------------------------------------------------

void TestSketchPersistence::aMovedReferenceIsFoundByFingerprint()
{
  // An index alone means nothing across sessions. Insert a node at the
  // front and every later index shifts by one; the fingerprint is what
  // lets the constraint find its own segment again.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString fem = dir.filePath("shifted.fem");

  FemmProblem p = sketchedRectangle();
  QString error;
  QVERIFY2(SketchFileIO::writeSketch(fem, p, error), qPrintable(error));

  // Rebuild the same geometry with an extra segment FIRST, so every
  // original segment index moves.
  FemmProblem shifted;
  shifted.problemType = FemmCoordinateType::Planar;
  const int e0 = FemmProblemEdit::addNode(shifted, -50, -50);
  const int e1 = FemmProblemEdit::addNode(shifted, -40, -50);
  FemmProblemEdit::addSegment(shifted, e0, e1); // segment 0, new
  const int n0 = FemmProblemEdit::addNode(shifted, 0, 0);
  const int n1 = FemmProblemEdit::addNode(shifted, 10, 0);
  const int n2 = FemmProblemEdit::addNode(shifted, 10, 6);
  const int n3 = FemmProblemEdit::addNode(shifted, 0, 6);
  FemmProblemEdit::addSegment(shifted, n0, n1); // was 0, now 1
  FemmProblemEdit::addSegment(shifted, n1, n2); // was 1, now 2
  FemmProblemEdit::addSegment(shifted, n2, n3);
  FemmProblemEdit::addSegment(shifted, n3, n0);

  QStringList report;
  QVERIFY2(SketchFileIO::readSketch(fem, shifted, report, error),
      qPrintable(error));
  QVERIFY2(report.isEmpty(), qPrintable(report.join("; ")));
  QCOMPARE(shifted.constraints.size(), 2);

  // The Horizontal constraint must now point at segment 1, the bottom
  // edge -- not at segment 0, which is where its stored index says.
  QCOMPARE((int)shifted.constraints[0].type, (int)ConstraintType::Horizontal);
  QCOMPARE(shifted.constraints[0].refA, 1);
  const FemmSegment& s = shifted.segments[shifted.constraints[0].refA];
  QCOMPARE(shifted.nodes[s.n0].x, 0.0);
  QCOMPARE(shifted.nodes[s.n1].x, 10.0);

  // and the dimension found its two nodes past the two inserted ones
  QCOMPARE(shifted.dimensions.size(), 1);
  QCOMPARE(shifted.nodes[shifted.dimensions[0].refA].x, 0.0);
  QCOMPARE(shifted.nodes[shifted.dimensions[0].refB].x, 10.0);
}

void TestSketchPersistence::aDeletedReferenceIsReportedNotReattached()
{
  // The requirement in the ticket's own words: sketch data whose geometry
  // no longer matches is reported, not silently applied. Silently
  // applying it would constrain a segment the user never chose.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString fem = dir.filePath("deleted.fem");

  FemmProblem p = sketchedRectangle();
  QString error;
  QVERIFY2(SketchFileIO::writeSketch(fem, p, error), qPrintable(error));

  // The same model with the constrained bottom edge gone.
  FemmProblem changed = p;
  changed.constraints.clear();
  changed.dimensions.clear();
  FemmProblemEdit::deleteSegment(changed, 0);

  QStringList report;
  QVERIFY2(SketchFileIO::readSketch(fem, changed, report, error),
      qPrintable(error));

  QVERIFY2(!report.isEmpty(),
      "a constraint on a deleted segment was restored without a word");
  // The Vertical constraint's segment is still there, so it survives --
  // only the one whose geometry went is dropped.
  QCOMPARE(changed.constraints.size(), 1);
  QCOMPARE((int)changed.constraints[0].type, (int)ConstraintType::Vertical);
  for (const FemmConstraint& c : changed.constraints) {
    QVERIFY2(c.refA >= 0 && c.refA < changed.segments.size(),
        "a restored constraint points outside the segment list");
  }
}

void TestSketchPersistence::anAmbiguousReferenceIsReportedRatherThanGuessed()
{
  // Two segments with identical endpoints: the fingerprint cannot say
  // which one the constraint meant. Picking the first would attach it to
  // geometry the user did not choose, and nothing downstream could tell.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString fem = dir.filePath("ambiguous.fem");

  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  FemmProblemEdit::addSegment(p, a, b);
  FemmConstraint h;
  h.type = ConstraintType::Horizontal;
  h.refA = 0;
  p.constraints.push_back(h);

  QString error;
  QVERIFY2(SketchFileIO::writeSketch(fem, p, error), qPrintable(error));

  // Now a model with the same segment twice, and the stored index
  // pointing at neither (both were renumbered by an insert).
  FemmProblem twin;
  twin.problemType = FemmCoordinateType::Planar;
  const int f0 = FemmProblemEdit::addNode(twin, -9, -9);
  const int f1 = FemmProblemEdit::addNode(twin, -8, -9);
  FemmProblemEdit::addSegment(twin, f0, f1);
  const int t0 = FemmProblemEdit::addNode(twin, 0, 0);
  const int t1 = FemmProblemEdit::addNode(twin, 10, 0);
  FemmProblemEdit::addSegment(twin, t0, t1);
  FemmProblemEdit::addSegment(twin, t0, t1); // an exact duplicate

  QStringList report;
  QVERIFY2(SketchFileIO::readSketch(fem, twin, report, error),
      qPrintable(error));

  QCOMPARE(twin.constraints.size(), 0);
  QVERIFY2(!report.isEmpty(), "an ambiguous reference was resolved silently");
  QVERIFY2(report.join(" ").contains("ambiguous"),
      qPrintable(QStringLiteral("the report does not say why: %1")
                     .arg(report.join("; "))));
}

void TestSketchPersistence::aNewerFormatIsRefusedRatherThanMisread()
{
  // A sidecar from a future version may mean anything. Reading it with
  // this build's assumptions would attach constraints by guesswork.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString fem = dir.filePath("future.fem");
  const QString fes = SketchFileIO::sidecarPathFor(fem);

  QFile f(fes);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
  QTextStream(&f) << "<Format> = 9999\n";
  f.close();

  FemmProblem p = sketchedRectangle();
  p.constraints.clear();
  p.dimensions.clear();
  QStringList report;
  QString error;
  QVERIFY2(!SketchFileIO::readSketch(fem, p, report, error),
      "a sidecar from a newer format version was read anyway");
  QVERIFY2(error.contains("newer"), qPrintable(error));
  QCOMPARE(p.constraints.size(), 0);
}

QTEST_GUILESS_MAIN(TestSketchPersistence)
#include "tst_sketch_persistence.moc"
