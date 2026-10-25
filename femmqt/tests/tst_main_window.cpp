// tst_main_window.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-10-25
// (issue #95).
//
// The editor window as a whole: what belongs to the DOCUMENT and must
// go when the document is replaced, rather than what any one dialog or
// scene does.
//
// Driven through the real MainWindow and its real actions. Messages go
// to stderr (Notify::setHeadless) so a regression that reaches a dialog
// fails instead of hanging. Every model is opened from a copy in a
// temporary directory, because opening writes a .femx cache beside the
// file and the demos directory is committed.

#include <QtTest>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>

#include "BlockLabelPropDialog.h"
#include "DemoLibrary.h"
#include "ProblemKind.h"
#include "MainWindow.h"
#include "Notify.h"

namespace {

QString demoPath(const QString& relative)
{
  return QStringLiteral(FEMMQT_SOURCE_DIR "/../demos/") + relative;
}

QAction* actionNamed(QWidget& w, const QString& text)
{
  for (QAction* a : w.findChildren<QAction*>()) {
    if (a->text() == text)
      return a;
  }
  return nullptr;
}

} // namespace

class TestMainWindow : public QObject
{
  Q_OBJECT

  QTemporaryDir m_dir;

  QString copyOf(const QString& demo)
  {
    const QString target = m_dir.filePath(QFileInfo(demo).fileName());
    QFile::remove(target);
    if (!QFile::copy(demoPath(demo), target))
      return {};
    return target;
  }

  private slots:
  void initTestCase();

  // #95: per-document state survives the document being replaced.
  void undoAfterOpenDoesNotBringBackThePreviousDocument();
  void undoAfterNewDoesNotBringBackThePreviousDocument();
  void undoStillWorksWithinOneDocument();
  void newAfterADemoDropsTheDemosTitle();

  // #99: the block label dialog offers <None> as well as <Hole>.
  void theBlockLabelDialogMapsNoneHoleAndMaterials();
};

void TestMainWindow::initTestCase()
{
  Notify::setHeadless(true);
  QVERIFY(m_dir.isValid());
  QVERIFY2(QFileInfo::exists(demoPath("magnetics/straight_wire.fem")), "demos/ not found");
}

void TestMainWindow::undoAfterOpenDoesNotBringBackThePreviousDocument()
{
  MainWindow w;
  QVERIFY(w.openFile(copyOf("magnetics/straight_wire.fem")));
  const int wireNodes = w.problem().nodes.size();
  // An edit in the first document puts its geometry on the undo stack.
  auto* scene = w.findChild<GeometryScene*>();
  QVERIFY(scene);
  emit scene->aboutToEdit();

  QVERIFY(w.openFile(copyOf("magnetics/coax_cable.fem")));
  const int coaxNodes = w.problem().nodes.size();
  QVERIFY2(coaxNodes != wireNodes, "the two models must be distinguishable");
  const QString title = w.windowTitle();

  QAction* undo = actionNamed(w, QStringLiteral("&Undo"));
  QVERIFY(undo);
  undo->trigger();

  QVERIFY2(w.problem().nodes.size() == coaxNodes,
      qPrintable(QStringLiteral("Undo after Open replaced the coax (%1 nodes) with %2 nodes "
                                "-- the previous file's geometry")
                     .arg(coaxNodes)
                     .arg(w.problem().nodes.size())));
  QCOMPARE(w.windowTitle(), title); // and did not mark it dirty
}

void TestMainWindow::undoAfterNewDoesNotBringBackThePreviousDocument()
{
  MainWindow w;
  QVERIFY(w.openFile(copyOf("magnetics/straight_wire.fem")));
  emit w.findChild<GeometryScene*>()->aboutToEdit();

  w.newDocument(FemmProblemKind::Electrostatics);
  QCOMPARE(w.problem().nodes.size(), 0);
  actionNamed(w, QStringLiteral("&Undo"))->trigger();
  QCOMPARE(w.problem().nodes.size(), 0);
  QCOMPARE(w.problem().kind, FemmProblemKind::Electrostatics);
  QVERIFY2(!w.windowTitle().contains('*'), qPrintable(w.windowTitle()));
}

void TestMainWindow::undoStillWorksWithinOneDocument()
{
  // The guard against fixing #95 by disabling Undo.
  MainWindow w;
  QVERIFY(w.openFile(copyOf("magnetics/straight_wire.fem")));
  const int before = w.problem().nodes.size();
  emit w.findChild<GeometryScene*>()->aboutToEdit();
  const_cast<FemmProblem&>(w.problem()).nodes.removeLast();
  QCOMPARE(w.problem().nodes.size(), before - 1);

  actionNamed(w, QStringLiteral("&Undo"))->trigger();
  QCOMPARE(w.problem().nodes.size(), before);
}

void TestMainWindow::newAfterADemoDropsTheDemosTitle()
{
  MainWindow w;
  QVERIFY(w.openDemo(demoPath("magnetics/straight_wire.fem"), QStringLiteral("Straight wire")));
  QVERIFY2(w.windowTitle().contains("Straight wire"), qPrintable(w.windowTitle()));

  w.newDocument(FemmProblemKind::Magnetics);
  QVERIFY2(!w.windowTitle().contains("Straight wire"),
      qPrintable(QStringLiteral("File > New kept the demo's name: \"%1\"").arg(w.windowTitle())));
  QVERIFY2(!w.windowTitle().contains("demo", Qt::CaseInsensitive), qPrintable(w.windowTitle()));
}

namespace {

// Opens the dialog on `label`, picks combo row `row` of the material box
// and presses OK. Returns the label's block type afterwards.
int pickMaterialRow(FemmBlockLabel label, const FemmProblem& p, int row, QString* shown = nullptr)
{
  BlockLabelPropDialog dlg({ &label }, p);
  QComboBox* material = dlg.findChildren<QComboBox*>().value(0);
  if (!material)
    return -999;
  if (shown)
    *shown = material->currentText();
  if (row >= 0)
    material->setCurrentIndex(row);
  dlg.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
  return label.blockTypeIndex;
}

} // namespace

void TestMainWindow::theBlockLabelDialogMapsNoneHoleAndMaterials()
{
  FemmProblem p;
  ProblemKind::addDefault(p, ProblemKind::Category::Material);
  ProblemKind::addDefault(p, ProblemKind::Category::Material);
  FemmBlockLabel l;

  QString shown;
  l.blockTypeIndex = 0;
  QCOMPARE(pickMaterialRow(l, p, -1, &shown), 0); // OK without touching it
  QCOMPARE(shown, QStringLiteral("<None>"));
  l.blockTypeIndex = -1;
  QCOMPARE(pickMaterialRow(l, p, -1, &shown), -1);
  QCOMPARE(shown, QStringLiteral("<Hole>"));
  l.blockTypeIndex = 2;
  pickMaterialRow(l, p, -1, &shown);
  QCOMPARE(shown, p.materialProps[1].name);

  l.blockTypeIndex = 0;
  QCOMPARE(pickMaterialRow(l, p, 1), -1); // <Hole>
  QCOMPARE(pickMaterialRow(l, p, 2), 1);  // first material
  QCOMPARE(pickMaterialRow(l, p, 3), 2);
  l.blockTypeIndex = 1;
  QCOMPARE(pickMaterialRow(l, p, 0), 0);  // back to <None>
}

QTEST_MAIN(TestMainWindow)
#include "tst_main_window.moc"
