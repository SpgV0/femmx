// tst_window_title.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-14.
//
// The title bar has to say which physics this document is.
//
// Per direct user request. Since #80 a document is one of four physics,
// chosen at File > New and unchangeable afterwards -- and nothing on
// screen said which one. The toolbar is identical for all four and the
// difference only surfaces once a property dialog is open, so a .fee
// and a .feh are indistinguishable at a glance.
//
// Tested as a function rather than by standing up a QMainWindow: the
// interesting part is the string, and a widget harness would make this
// slower and no more truthful.

#include <QtTest>

#include "ProblemKind.h"
#include "WindowTitle.h"

class TestWindowTitle : public QObject
{
  Q_OBJECT

  private slots:
  void everyPhysicsIsNamedInTheTitle();
  void everyPhysicsIsNamedInTheTitle_data();

  void anUntitledDocumentStillSaysWhichPhysics();
  void theDirtyMarkerIsTheLastThing();
  void aDemoCopyShowsItsNameRatherThanATempPath();
  void theViewerAndTheEditorAgreeOnWhatToCallAPhysics();
};

void TestWindowTitle::everyPhysicsIsNamedInTheTitle_data()
{
  QTest::addColumn<int>("kind");
  QTest::addColumn<QString>("expected");

  // The names the rest of the app already uses, so the title cannot
  // invent a fifth vocabulary for the same four things.
  QTest::newRow("magnetics") << (int)FemmProblemKind::Magnetics << "Magnetics";
  QTest::newRow("electrostatics")
      << (int)FemmProblemKind::Electrostatics << "Electrostatics";
  QTest::newRow("heat flow") << (int)FemmProblemKind::HeatFlow << "Heat Flow";
  QTest::newRow("current flow")
      << (int)FemmProblemKind::CurrentFlow << "Current Flow";
}

void TestWindowTitle::everyPhysicsIsNamedInTheTitle()
{
  QFETCH(int, kind);
  QFETCH(QString, expected);
  const FemmProblemKind k = (FemmProblemKind)kind;

  const QString title = WindowTitle::forEditor(k, "C:/models/thing.fem", false);
  QVERIFY2(title.contains(expected),
      qPrintable(QStringLiteral("\"%1\" does not name the physics").arg(title)));
  QCOMPARE(expected, ProblemKind::displayName(k));

  // And the file is still there -- naming the physics must not cost the
  // thing a title bar is mostly for.
  QVERIFY2(title.contains(QStringLiteral("C:/models/thing.fem")),
      qPrintable(title));
  QVERIFY2(title.startsWith(QStringLiteral("FEMMX (Qt)")), qPrintable(title));

  // No other physics' name may appear: "Current Flow" and "Heat Flow"
  // share a word, and a sloppy join could show both.
  for (FemmProblemKind other : { FemmProblemKind::Magnetics,
           FemmProblemKind::Electrostatics, FemmProblemKind::HeatFlow,
           FemmProblemKind::CurrentFlow }) {
    if (other == k)
      continue;
    QVERIFY2(!title.contains(ProblemKind::displayName(other)),
        qPrintable(QStringLiteral("a %1 title also says \"%2\": %3")
                       .arg(expected, ProblemKind::displayName(other), title)));
  }
}

void TestWindowTitle::anUntitledDocumentStillSaysWhichPhysics()
{
  // The state the application starts in, and the one the request came
  // from: a brand new document showed "FEMMX (Qt) - Untitled" and
  // nothing else.
  const QString title =
      WindowTitle::forEditor(FemmProblemKind::HeatFlow, QString(), false);
  QVERIFY2(title.contains(QStringLiteral("Heat Flow")), qPrintable(title));
  QVERIFY2(title.contains(QStringLiteral("Untitled")), qPrintable(title));
}

void TestWindowTitle::theDirtyMarkerIsTheLastThing()
{
  const QString clean =
      WindowTitle::forEditor(FemmProblemKind::Magnetics, "a.fem", false);
  const QString dirty =
      WindowTitle::forEditor(FemmProblemKind::Magnetics, "a.fem", true);

  QVERIFY2(!clean.endsWith('*'), qPrintable(clean));
  QVERIFY2(dirty.endsWith('*'), qPrintable(dirty));
  // Exactly one character of difference: an asterisk that landed in the
  // middle, or twice, would be a different bug with the same symptom.
  QCOMPARE(dirty, clean + "*");
}

void TestWindowTitle::aDemoCopyShowsItsNameRatherThanATempPath()
{
  // #91: a demo opens as a working copy in a temp directory. Showing
  // that path would be noise, and it would make Save asking for a
  // location look like a bug rather than the point.
  const QString title = WindowTitle::forEditor(FemmProblemKind::HeatFlow,
      "C:/Users/x/AppData/Local/Temp/femmqt-demos/slab_1d-2026/slab_1d.feh",
      true, QStringLiteral("1-D slab"));

  QVERIFY2(title.contains(QStringLiteral("1-D slab")), qPrintable(title));
  QVERIFY2(title.contains(QStringLiteral("demo copy")), qPrintable(title));
  QVERIFY2(!title.contains(QStringLiteral("Temp")),
      qPrintable(QStringLiteral("the temp path leaked into the title: %1")
                     .arg(title)));
  QVERIFY2(title.contains(QStringLiteral("Heat Flow")), qPrintable(title));
  QVERIFY2(title.endsWith('*'), qPrintable(title));
}

void TestWindowTitle::theViewerAndTheEditorAgreeOnWhatToCallAPhysics()
{
  // The viewer said "FEMMX (Qt) - Solution Viewer - <path>" for
  // magnetics and "<Kind> Solution -- <file>" for the other three: two
  // answers to the same question, depending on which file you opened.
  for (FemmProblemKind k : { FemmProblemKind::Magnetics,
           FemmProblemKind::Electrostatics, FemmProblemKind::HeatFlow,
           FemmProblemKind::CurrentFlow }) {
    const QString solution = WindowTitle::forSolution(k, "C:/out/model.ans");
    QVERIFY2(solution.contains(ProblemKind::displayName(k)), qPrintable(solution));
    QVERIFY2(solution.startsWith(QStringLiteral("FEMMX (Qt)")), qPrintable(solution));
    QVERIFY2(solution.contains(QStringLiteral("Solution")), qPrintable(solution));
    // The file name, not the directory it happened to be chosen from.
    QVERIFY2(solution.contains(QStringLiteral("model.ans")), qPrintable(solution));
    QVERIFY2(!solution.contains(QStringLiteral("C:/out")), qPrintable(solution));
  }
}

QTEST_GUILESS_MAIN(TestWindowTitle)
#include "tst_window_title.moc"
