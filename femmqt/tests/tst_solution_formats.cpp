// tst_solution_formats.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #83).
//
// Reading the [Solution] section of the four solution formats.
//
// The failure this file is written against is a COLUMN SHIFT, not a
// parse error. Every one of these files is whitespace-separated numbers,
// so reading the wrong number of columns per node does not fail: it
// consumes the next line's x as this line's last field and carries the
// misalignment down the whole mesh. The result is a solution that loads,
// plots, and is wrong everywhere.
//
// Two places that can happen, both taken from the classic
// post-processors:
//
//   * magnetics writes A's imaginary part ONLY when the problem's
//     frequency is non-zero, so a .ans has three node columns or four
//     depending on a value in its own header;
//   * the three non-magnetics formats carry a trailing conductor index
//     that magnetics does not have.
//
// So the cases below build files with deliberately distinctive values
// and check the LAST field of the LAST node -- the place a shift of one
// column anywhere above would show up.

#include <QtTest>

#include "FemmProblem.h"
#include "ProblemKind.h"
#include "SolutionFileIO.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

namespace {

// A minimal but valid model header for `kind`, so the reader's model
// half has something real to parse, followed by a [Solution] section
// with the given node and element lines.
void writeSolutionFile(const QString& path, FemmProblemKind kind, double frequency,
    const QStringList& nodeLines, const QStringList& elementLines)
{
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
  QTextStream out(&f);
  out << "[Format] = 4.0\n";
  out << "[Precision] = 1e-08\n";
  if (kind == FemmProblemKind::Magnetics || kind == FemmProblemKind::CurrentFlow)
    out << "[Frequency] = " << frequency << "\n";
  out << "[LengthUnits] = millimeters\n";
  out << "[ProblemType] = planar\n";
  // One block label, so an element's label index resolves.
  out << "[NumPoints] = 0\n";
  out << "[NumSegments] = 0\n";
  out << "[NumArcSegments] = 0\n";
  out << "[NumHoles] = 0\n";
  if (kind == FemmProblemKind::Magnetics)
    out << "[NumBlockLabels] = 1\n0\t0\t1\t-1\t0\t0\t0\t0\t0\n";
  else
    out << "[NumBlockLabels] = 1\n0\t0\t1\t-1\t0\t0\n";
  out << "[Solution] = 0\n";
  out << nodeLines.size() << "\n";
  for (const QString& l : nodeLines)
    out << l << "\n";
  out << elementLines.size() << "\n";
  for (const QString& l : elementLines)
    out << l << "\n";
}

} // namespace

class TestSolutionFormats : public QObject
{
  Q_OBJECT

  private slots:
  void aDcMagneticsSolutionHasThreeNodeColumns();
  void anAcMagneticsSolutionHasFourNodeColumns();
  void theOtherThreeCarryAConductorColumn();
  void theOtherThreeCarryAConductorColumn_data();
  void elementsAreTheSameInEveryFormat();
  void aModelFileIsNotASolutionFile();
  void aTruncatedMeshIsReportedRatherThanReturnedShort();
  void aShortElementSectionIsReportedToo();
  void solutionExtensionsNeverResolveToAModelKind();
};

void TestSolutionFormats::aDcMagneticsSolutionHasThreeNodeColumns()
{
  // Frequency 0: x y A. If the reader assumed four columns it would take
  // the second node's x (7.5) as the first node's A_im and everything
  // after would slide.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.path() + "/dc.ans";
  writeSolutionFile(path, FemmProblemKind::Magnetics, 0.0,
      { "1.5\t2.5\t3.5", "7.5\t8.5\t9.5" }, { "0\t1\t0\t0" });

  FemmProblem p;
  SolvedMesh mesh;
  QString error;
  QVERIFY2(SolutionFileIO::read(path, p, mesh, error), qPrintable(error));

  QCOMPARE(mesh.nodes.size(), 2);
  QCOMPARE(mesh.nodes[0].x, 1.5);
  QCOMPARE(mesh.nodes[0].y, 2.5);
  QCOMPARE(mesh.nodes[0].potentialRe, 3.5);
  QCOMPARE(mesh.nodes[0].potentialIm, 0.0);
  // The tell: node 1 still starts where it should.
  QCOMPARE(mesh.nodes[1].x, 7.5);
  QCOMPARE(mesh.nodes[1].potentialRe, 9.5);
}

void TestSolutionFormats::anAcMagneticsSolutionHasFourNodeColumns()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.path() + "/ac.ans";
  writeSolutionFile(path, FemmProblemKind::Magnetics, 60.0,
      { "1.5\t2.5\t3.5\t4.5", "7.5\t8.5\t9.5\t10.5" }, { "0\t1\t0\t0" });

  FemmProblem p;
  SolvedMesh mesh;
  QString error;
  QVERIFY2(SolutionFileIO::read(path, p, mesh, error), qPrintable(error));

  QCOMPARE(p.frequency, 60.0);
  QCOMPARE(mesh.nodes.size(), 2);
  QCOMPARE(mesh.nodes[0].potentialRe, 3.5);
  QCOMPARE(mesh.nodes[0].potentialIm, 4.5);
  QCOMPARE(mesh.nodes[1].potentialIm, 10.5);
  // Magnetics has no conductor column at all.
  QCOMPARE(mesh.nodes[0].conductor, 0);
  QCOMPARE(mesh.nodes[1].conductor, 0);
}

void TestSolutionFormats::theOtherThreeCarryAConductorColumn_data()
{
  QTest::addColumn<int>("kindValue");
  QTest::addColumn<QString>("nodeLine");
  QTest::addColumn<double>("expectedRe");
  QTest::addColumn<double>("expectedIm");
  QTest::addColumn<int>("expectedConductor");

  // .res and .anh: x y potential conductor.
  QTest::newRow("electrostatics .res")
      << (int)FemmProblemKind::Electrostatics << "1.5\t2.5\t12.5\t3" << 12.5 << 0.0 << 3;
  QTest::newRow("heat flow .anh")
      << (int)FemmProblemKind::HeatFlow << "1.5\t2.5\t300.5\t4" << 300.5 << 0.0 << 4;
  // .anc is the only format that is complex AND has a conductor column.
  QTest::newRow("current flow .anc")
      << (int)FemmProblemKind::CurrentFlow << "1.5\t2.5\t12.5\t13.5\t5" << 12.5 << 13.5 << 5;
}

void TestSolutionFormats::theOtherThreeCarryAConductorColumn()
{
  QFETCH(int, kindValue);
  QFETCH(QString, nodeLine);
  QFETCH(double, expectedRe);
  QFETCH(double, expectedIm);
  QFETCH(int, expectedConductor);
  const FemmProblemKind kind = (FemmProblemKind)kindValue;

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path =
      dir.path() + "/s." + ProblemKind::solutionExtension(kind);
  writeSolutionFile(path, kind, 50.0, { nodeLine }, { "0\t1\t2\t0" });

  FemmProblem p;
  SolvedMesh mesh;
  QString error;
  QVERIFY2(SolutionFileIO::read(path, p, mesh, error), qPrintable(error));

  QCOMPARE(mesh.nodes.size(), 1);
  QCOMPARE(mesh.nodes[0].potentialRe, expectedRe);
  QCOMPARE(mesh.nodes[0].potentialIm, expectedIm);
  // The conductor is the LAST field, so it is where a shift anywhere to
  // its left would surface.
  QCOMPARE(mesh.nodes[0].conductor, expectedConductor);
}

void TestSolutionFormats::elementsAreTheSameInEveryFormat()
{
  for (FemmProblemKind kind : { FemmProblemKind::Magnetics,
           FemmProblemKind::Electrostatics, FemmProblemKind::HeatFlow,
           FemmProblemKind::CurrentFlow }) {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/s." + ProblemKind::solutionExtension(kind);
    const QString node = (kind == FemmProblemKind::Magnetics)
        ? QStringLiteral("1.5\t2.5\t3.5")
        : (kind == FemmProblemKind::CurrentFlow ? QStringLiteral("1.5\t2.5\t3.5\t4.5\t1")
                                                : QStringLiteral("1.5\t2.5\t3.5\t1"));
    writeSolutionFile(path, kind, 0.0, { node, node, node },
        { "0\t1\t2\t0", "2\t1\t0\t0" });

    FemmProblem p;
    SolvedMesh mesh;
    QString error;
    QVERIFY2(SolutionFileIO::read(path, p, mesh, error), qPrintable(error));
    QCOMPARE(mesh.elements.size(), 2);
    QCOMPARE(mesh.elements[0].p0, 0);
    QCOMPARE(mesh.elements[0].p1, 1);
    QCOMPARE(mesh.elements[0].p2, 2);
    QCOMPARE(mesh.elements[0].label, 0);
    QCOMPARE(mesh.elements[1].p0, 2);
    QCOMPARE(mesh.elements[1].p2, 0);
  }
}

void TestSolutionFormats::aModelFileIsNotASolutionFile()
{
  // An unsolved model and a solution differ only by the appended
  // section. Returning an empty mesh for the former would make "not
  // solved yet" indistinguishable from "solved, and empty".
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.path() + "/model.ans";
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
  QTextStream out(&f);
  out << "[Format] = 4.0\n[Frequency] = 0\n[NumPoints] = 0\n";
  f.close();

  FemmProblem p;
  SolvedMesh mesh;
  QString error;
  QVERIFY2(!SolutionFileIO::read(path, p, mesh, error),
      "a file with no [Solution] section was read as a solution");
  QVERIFY2(error.contains("[Solution]"), qPrintable(error));
}

void TestSolutionFormats::aTruncatedMeshIsReportedRatherThanReturnedShort()
{
  // The count and the rows are written separately, so a truncated file
  // yields fewer rows than it promised. Handing back the short mesh
  // would plot a solution with a hole in it.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.path() + "/short.anh";
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
  QTextStream out(&f);
  out << "[Format] = 4.0\n[NumPoints] = 0\n[Solution] = 0\n";
  out << "5\n"; // claims five nodes
  out << "1\t2\t3\t0\n2\t3\t4\t0\n"; // supplies two
  f.close();

  FemmProblem p;
  SolvedMesh mesh;
  QString error;
  QVERIFY2(!SolutionFileIO::read(path, p, mesh, error),
      "a truncated mesh was returned short instead of reported");
  // Deliberately not asserting the exact wording. A node section that
  // stops early runs off the end of the file, so the reader notices when
  // the element section is not there -- which is a more specific
  // diagnosis than "malformed", and asserting the word "truncated" here
  // only pinned my own guess about which branch would fire.
  QVERIFY2(!error.isEmpty(), "a truncated file failed silently");
  QVERIFY2(error.contains("short.anh"),
      qPrintable(QStringLiteral("the error should name the file; got: %1").arg(error)));
}

void TestSolutionFormats::aShortElementSectionIsReportedToo()
{
  // The other half of the same problem, and the one that reaches the
  // count check: the node section is complete, so the reader gets as far
  // as the element count, and then the rows run out.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.path() + "/shortelem.res";
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
  QTextStream out(&f);
  out << "[Format] = 4.0\n[NumPoints] = 0\n[Solution] = 0\n";
  out << "2\n1\t2\t3\t0\n2\t3\t4\t0\n";
  out << "4\n"; // claims four elements
  out << "0\t1\t2\t0\n"; // supplies one
  f.close();

  FemmProblem p;
  SolvedMesh mesh;
  QString error;
  QVERIFY2(!SolutionFileIO::read(path, p, mesh, error),
      "an element section short of its own count was accepted");
  QVERIFY2(error.contains("4") && error.contains("1"),
      qPrintable(QStringLiteral("the error should say how many were promised and "
                                "how many arrived; got: %1").arg(error)));
}

void TestSolutionFormats::solutionExtensionsNeverResolveToAModelKind()
{
  // .ans and .fem must not be interchangeable: one has a [Solution]
  // section and one does not, and the readers differ accordingly.
  FemmProblemKind kind = FemmProblemKind::CurrentFlow;
  QVERIFY(SolutionFileIO::kindForSolutionPath("a.ans", kind));
  QCOMPARE((int)kind, (int)FemmProblemKind::Magnetics);
  QVERIFY(SolutionFileIO::kindForSolutionPath("a.res", kind));
  QCOMPARE((int)kind, (int)FemmProblemKind::Electrostatics);
  QVERIFY(SolutionFileIO::kindForSolutionPath("a.anh", kind));
  QCOMPARE((int)kind, (int)FemmProblemKind::HeatFlow);
  QVERIFY(SolutionFileIO::kindForSolutionPath("a.anc", kind));
  QCOMPARE((int)kind, (int)FemmProblemKind::CurrentFlow);

  // Model extensions are not solutions.
  QVERIFY(!SolutionFileIO::kindForSolutionPath("a.fem", kind));
  QVERIFY(!SolutionFileIO::kindForSolutionPath("a.fee", kind));
  QVERIFY(!SolutionFileIO::kindForSolutionPath("a.feh", kind));
  QVERIFY(!SolutionFileIO::kindForSolutionPath("a.fec", kind));
  QVERIFY(!SolutionFileIO::kindForSolutionPath("a.ansx", kind));
}

QTEST_GUILESS_MAIN(TestSolutionFormats)
#include "tst_solution_formats.moc"
