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

#include <QCheckBox>
#include <QAbstractButton>
#include <QComboBox>
#include <QDockWidget>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QMessageBox>
#include <QLabel>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QInputDialog>
#include <QTimer>
#include <QLineEdit>
#include <QGraphicsScene>
#include <QPainter>
#include <QDialogButtonBox>
#include <QPushButton>

#include "AnsFileIO.h"
#include "SolutionAdapter.h"
#include "SolutionFileIO.h"
#include "AppTheme.h"
#include "ConstraintSolver.h"
#include "FemmProblemEdit.h"
#include "ProblemFileIO.h"
#include "SketchFileIO.h"
#include "BlockLabelPropDialog.h"
#include "SolutionView.h"
#include "DemoLibrary.h"
#include "FileDialogDirs.h"
#include "ProblemKind.h"
#include "GeometryView.h"
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

  // #100: Show Mesh over a density plot filled every element.
  void showMeshOverADensityPlotDrawsOnlyOutlines();

  // #101: light theme -- unchecked checkboxes and input fields vanished.
  void anUncheckedCheckboxIsVisibleInBothThemes();
  void anInputFieldStandsOutFromThePanelInBothThemes();

  // Review FQT-06/07: editing a dimension through the real window.
  void editingAnAngleDimensionThenUndoRestoresTheValueToo();
  void aReopenedConsistentSketchIsNotShownAsConflicting();
  void undoRestoresTheConstraintColoursWithTheSketch();
  void solvingAnAlreadySatisfiedSketchIsNotAnEdit();
  // #103
  void anImpossibleDimensionEditIsRefusedThroughTheWindow();
  // #106
  void fixFromTheMenuHoldsTheSelectedNodesInOneUndoStep();
  // #108
  void areaPropertiesShowsTheForceOnAWire();
  // #109
  void redoBringsBackWhatUndoTookBack();
  void theUnsavedChangesPromptCanSave();
  // #110
  void fileDialogsStartWhereTheWorkIs();
  void noFileDialogStartsInTheWorkingDirectory();
  void theInstalledShortcutsStartInDocuments();
  // Review FQT-24: not reproduced; kept as a guard.
  void homeStillFitsAfterADialog();
  // #112
  void theGridIsOnButNotInRenders();
  void zoomStepsAreGentleAndFitLeavesAMargin();
  // #113
  void aUniformFieldPaintsOneColourNotNoise();
  // #115
  void pointPropertiesIsAPanelNotAModalPerClick();
  void theSolutionWindowRefitsOnResizeUntilTheUserZooms();
  void anAreaSelectionLetsThePlotShowThrough();
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

namespace {

QImage renderSolution(MeshSolutionItem& item, const MeshSolution& sol)
{
  QGraphicsScene scene;
  scene.addItem(&item);
  QRectF bounds = item.boundingRect();
  QImage img(400, 400, QImage::Format_RGB32);
  img.fill(Qt::white);
  {
    QPainter painter(&img);
    scene.render(&painter, QRectF(img.rect()), bounds);
  }
  scene.removeItem(&item);
  Q_UNUSED(sol);
  return img;
}

} // namespace

void TestMainWindow::showMeshOverADensityPlotDrawsOnlyOutlines()
{
  FemmProblem p;
  MeshSolution sol;
  QString err;
  QVERIFY2(AnsFileIO::readAns(QStringLiteral(FEMMQT_TEST_FIXTURE_DIR "/render/render_fixture.ans"), p, sol, err),
      qPrintable(err));
  MeshSolutionItem item(&sol);
  item.setProblemGeometry(&p);
  item.setPlotMode(MeshSolutionItem::PlotMode::Density);

  item.setShowMesh(false);
  const QImage plain = renderSolution(item, sol);
  item.setShowMesh(true);
  const QImage meshed = renderSolution(item, sol);

  // The mesh lines are hairlines, so they may change a good share of the
  // pixels on a dense mesh -- but the colours between them must survive.
  // Filled, every element is one colour and the density plot is gone.
  QSet<QRgb> plainColours, meshedColours;
  int changed = 0;
  for (int y = 0; y < plain.height(); y++) {
    for (int x = 0; x < plain.width(); x++) {
      plainColours.insert(plain.pixel(x, y));
      meshedColours.insert(meshed.pixel(x, y));
      if (plain.pixel(x, y) != meshed.pixel(x, y))
        changed++;
    }
  }
  QVERIFY2(plainColours.size() > 8, "precondition: the density plot has several colours");
  QVERIFY2(meshedColours.size() > plainColours.size() / 2,
      qPrintable(QStringLiteral("with Show Mesh on the plot has %1 colours, against %2 without "
                                "-- the mesh was filled over it")
                     .arg(meshedColours.size()).arg(plainColours.size())));
  const double changedShare = double(changed) / (plain.width() * plain.height());
  QVERIFY2(changedShare < 0.5, qPrintable(QStringLiteral("%1% of pixels changed").arg(changedShare * 100, 0, 'f', 1)));
}

namespace {

int luminance(QRgb c)
{
  return (qRed(c) * 299 + qGreen(c) * 587 + qBlue(c) * 114) / 1000;
}

// The largest luminance difference between any pixel of `img` inside
// `area` and the colour `against`.
int maxContrast(const QImage& img, QRect area, QColor against)
{
  int best = 0;
  area = area.intersected(img.rect());
  for (int y = area.top(); y <= area.bottom(); y++)
    for (int x = area.left(); x <= area.right(); x++)
      best = std::max(best, std::abs(luminance(img.pixel(x, y)) - luminance(against.rgb())));
  return best;
}

} // namespace

void TestMainWindow::anUncheckedCheckboxIsVisibleInBothThemes()
{
  for (bool dark : { false, true }) {
    AppTheme::setDark(dark);
    QCheckBox box(QStringLiteral(" "));
    box.setChecked(false);
    box.resize(box.sizeHint());
    const QImage img = box.grab().toImage();
    // The indicator is the square at the left; the label is blank.
    const QRect indicator(0, 0, std::min(img.width(), img.height()), img.height());
    const int contrast = maxContrast(img, indicator, qApp->palette().color(QPalette::Window));
    QVERIFY2(contrast >= 60,
        qPrintable(QStringLiteral("%1 theme: the unchecked box differs from the panel by only %2 "
                                  "luminance levels -- it reads as no checkbox at all")
                       .arg(dark ? "dark" : "light").arg(contrast)));
  }
  AppTheme::setDark(false);
}

void TestMainWindow::anInputFieldStandsOutFromThePanelInBothThemes()
{
  for (bool dark : { false, true }) {
    AppTheme::setDark(dark);
    const QPalette p = qApp->palette();
    const int fill = std::abs(luminance(p.color(QPalette::Base).rgb()) - luminance(p.color(QPalette::Window).rgb()));
    QVERIFY2(fill >= 8,
        qPrintable(QStringLiteral("%1 theme: Base and Window differ by %2 -- a field is the panel's colour")
                       .arg(dark ? "dark" : "light").arg(fill)));
    QLineEdit edit;
    edit.resize(120, edit.sizeHint().height());
    const QImage img = edit.grab().toImage();
    const int frame = maxContrast(img, QRect(0, 0, img.width(), 2), p.color(QPalette::Window));
    QVERIFY2(frame >= 40,
        qPrintable(QStringLiteral("%1 theme: the field's frame differs from the panel by %2")
                       .arg(dark ? "dark" : "light").arg(frame)));
  }
  AppTheme::setDark(false);
}

namespace {

// The review's trapezoid, with the angle dimension Smart Dimension makes
// when you click the left side and then the bottom: vertex 0, rays to 3
// and 1, so it measures -75.96 degrees as stored today.
void trapezoidWithAngle(FemmProblem& p, double storedValue)
{
  FemmProblemEdit::addNode(p, 0, 0);
  FemmProblemEdit::addNode(p, 70, 0);
  FemmProblemEdit::addNode(p, 60, 40);
  FemmProblemEdit::addNode(p, 10, 40);
  FemmProblemEdit::addSegment(p, 0, 1);
  FemmProblemEdit::addSegment(p, 1, 2);
  FemmProblemEdit::addSegment(p, 2, 3);
  FemmProblemEdit::addSegment(p, 3, 0);
  FemmDimension d;
  d.type = DimensionType::Angle;
  d.refA = 0;
  d.refB = 3;
  d.refC = 1;
  d.value = storedValue;
  d.labelOffsetX = 1;
  d.labelOffsetY = 1;
  p.dimensions << d;
}

// Answers the next modal QInputDialog with `value`.
void answerNextInputDialog(double value)
{
  QTimer::singleShot(0, [value]() {
    auto* dlg = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
    if (!dlg)
      return;
    dlg->setDoubleValue(value);
    dlg->accept();
  });
}

QPointF dimensionPos(GeometryScene& scene)
{
  // The label text: solid under its whole rect, unlike the thin arc.
  for (QGraphicsItem* item : scene.items()) {
    if (item->type() == QGraphicsSimpleTextItem::Type && item->data(0).isValid()
        && item->data(0).toInt() == static_cast<int>(FemmItemKind::Dimension)) {
      // The text ignores the view transform, so its scene rect is not
      // where it is drawn; go through the view the way a click does.
      QGraphicsView* view = scene.views().first();
      const QRectF device = item->deviceTransform(view->viewportTransform()).mapRect(item->boundingRect());
      return view->mapToScene(device.center().toPoint());
    }
  }
  return QPointF(qQNaN(), qQNaN());
}

} // namespace

void TestMainWindow::editingAnAngleDimensionThenUndoRestoresTheValueToo()
{
  MainWindow w;
  w.newDocument(FemmProblemKind::Magnetics);
  auto& p = const_cast<FemmProblem&>(w.problem());
  trapezoidWithAngle(p, -75.963756532073536);
  auto* scene = w.findChild<GeometryScene*>();
  scene->rebuild();
  w.resize(800, 600);
  w.show();
  QVERIFY(QTest::qWaitForWindowExposed(&w));

  const QPointF at = dimensionPos(*scene);
  QVERIFY(!qIsNaN(at.x()));
  const QPointF before3(p.nodes[3].x, p.nodes[3].y);

  answerNextInputDialog(80);
  QGraphicsSceneMouseEvent dbl(QEvent::GraphicsSceneMouseDoubleClick);
  dbl.setScenePos(at);
  dbl.setButton(Qt::LeftButton);
  dbl.setButtons(Qt::LeftButton);
  QApplication::sendEvent(scene, &dbl);
  QVERIFY2(std::abs(std::abs(p.dimensions[0].value) - 80) < 1e-9,
      qPrintable(QStringLiteral("the edit did not take: value %1").arg(p.dimensions[0].value)));
  // #105: the label reads the magnitude, and the vertex stayed on its
  // side -- typing 80 for a -75.96 corner used to make a bow-tie.
  for (QGraphicsItem* item : scene->items()) {
    if (auto* t = qgraphicsitem_cast<QGraphicsSimpleTextItem*>(item)) {
      if (t->data(0).toInt() == static_cast<int>(FemmItemKind::Dimension) && t->data(0).isValid())
        QVERIFY2(!t->text().startsWith('-'), qPrintable(t->text()));
    }
  }
  QVERIFY2(p.nodes[3].y > 0, "the corner swung below the bottom edge");

  QAction* undo = actionNamed(w, QStringLiteral("&Undo"));
  undo->trigger();
  QVERIFY2(std::abs(p.dimensions[0].value + 75.963756532073536) < 1e-6 || std::abs(p.dimensions[0].value - 75.963756532073536) < 1e-6,
      qPrintable(QStringLiteral("Undo put the geometry back but left the value at %1").arg(p.dimensions[0].value)));
  QCOMPARE(QPointF(p.nodes[3].x, p.nodes[3].y), before3);
}

namespace {

// Two consistent distance dimensions and a horizontal constraint on an
// open L, saved with its sketch sidecar the way Save writes it.
QString writeConsistentSketch(const QString& dir)
{
  FemmProblem p;
  FemmProblemEdit::addNode(p, 0, 0);
  FemmProblemEdit::addNode(p, 70, 0);
  FemmProblemEdit::addNode(p, 70, 30);
  FemmProblemEdit::addSegment(p, 0, 1);
  FemmProblemEdit::addSegment(p, 1, 2);
  FemmConstraint h;
  h.type = ConstraintType::Horizontal;
  h.refA = 0;
  p.constraints << h;
  FemmDimension a;
  a.type = DimensionType::Distance;
  a.refA = 0;
  a.refB = 1;
  a.value = 70;
  p.dimensions << a;
  FemmDimension b = a;
  b.refA = 1;
  b.refB = 2;
  b.value = 30;
  p.dimensions << b;
  const QString path = dir + QStringLiteral("/consistent.fem");
  QString err;
  if (!ProblemFileIO::write(path, p, err) || !SketchFileIO::writeSketch(path, p, err))
    return {};
  return path;
}

} // namespace

void TestMainWindow::aReopenedConsistentSketchIsNotShownAsConflicting()
{
  const QString path = writeConsistentSketch(m_dir.path());
  QVERIFY(!path.isEmpty());
  MainWindow w;
  QVERIFY(w.openFile(path));
  QCOMPARE(w.problem().dimensions.size(), 2);
  const auto& status = w.findChild<GeometryScene*>()->constraintStatus();
  QVERIFY2(!status.isEmpty(), "a reopened sketch had no constraint status at all");
  for (auto it = status.begin(); it != status.end(); ++it)
    QVERIFY2(it.value() != ConstraintSolver::SketchStatus::Conflicting,
        qPrintable(QStringLiteral("node %1 shows as conflicting in a sketch that is satisfied").arg(it.key())));
  QVERIFY2(!w.windowTitle().contains('*'), qPrintable(w.windowTitle()));
}

void TestMainWindow::undoRestoresTheConstraintColoursWithTheSketch()
{
  const QString path = writeConsistentSketch(m_dir.path());
  MainWindow w;
  QVERIFY(w.openFile(path));
  auto* scene = w.findChild<GeometryScene*>();
  const auto before = scene->constraintStatus();

  // Clear All Dimensions, then Undo: the dimensions come back, and so
  // must the colours they imply.
  actionNamed(w, QStringLiteral("Clear All Dimensions"))->trigger();
  QCOMPARE(w.problem().dimensions.size(), 0);
  actionNamed(w, QStringLiteral("&Undo"))->trigger();
  QCOMPARE(w.problem().dimensions.size(), 2);
  QCOMPARE(scene->constraintStatus(), before);
}

void TestMainWindow::solvingAnAlreadySatisfiedSketchIsNotAnEdit()
{
  const QString path = writeConsistentSketch(m_dir.path());
  MainWindow w;
  QVERIFY(w.openFile(path));
  actionNamed(w, QStringLiteral("&Solve Constraints"))->trigger();
  QVERIFY2(!w.windowTitle().contains('*'),
      qPrintable(QStringLiteral("Solve Constraints on a satisfied sketch marked it dirty: %1").arg(w.windowTitle())));
  // and cost no undo step: Undo now has nothing to do
  const FemmProblem before = w.problem();
  actionNamed(w, QStringLiteral("&Undo"))->trigger();
  QCOMPARE(w.problem().nodes.size(), before.nodes.size());
  QVERIFY(!w.windowTitle().contains('*'));
}

void TestMainWindow::anImpossibleDimensionEditIsRefusedThroughTheWindow()
{
  // The consistent L's bottom edge is Horizontal and dimensioned 70.
  // Editing its OTHER dimension (the 30 upright) is fine; making the
  // horizontal edge's own dimension negative-impossible is not something
  // the dialog allows, so conflict it through a vertical dimension.
  const QString path = writeConsistentSketch(m_dir.path());
  MainWindow w;
  QVERIFY(w.openFile(path));
  auto& p = const_cast<FemmProblem&>(w.problem());
  FemmDimension v;
  v.type = DimensionType::VerticalDistance;
  v.refA = 0;
  v.refB = 1;
  v.value = 0;
  v.labelOffsetX = 5;
  v.labelOffsetY = -5;
  p.dimensions << v;
  auto* scene = w.findChild<GeometryScene*>();
  scene->rebuild();
  w.resize(800, 600);
  w.show();
  QVERIFY(QTest::qWaitForWindowExposed(&w));
  const FemmProblem before = p;
  const QString title = w.windowTitle();

  // Double-click the vertical dimension's label (the last one added).
  QPointF at(qQNaN(), qQNaN());
  for (QGraphicsItem* item : scene->items()) {
    if (item->type() == QGraphicsSimpleTextItem::Type && item->data(0).toInt() == static_cast<int>(FemmItemKind::Dimension)
        && item->data(1).toInt() == 2) {
      QGraphicsView* view = scene->views().first();
      const QRectF device = item->deviceTransform(view->viewportTransform()).mapRect(item->boundingRect());
      at = view->mapToScene(device.center().toPoint());
    }
  }
  QVERIFY(!qIsNaN(at.x()));
  answerNextInputDialog(10);
  QGraphicsSceneMouseEvent dbl(QEvent::GraphicsSceneMouseDoubleClick);
  dbl.setScenePos(at);
  dbl.setButton(Qt::LeftButton);
  dbl.setButtons(Qt::LeftButton);
  QApplication::sendEvent(scene, &dbl);

  QCOMPARE(p.dimensions[2].value, 0.0);
  for (int i = 0; i < p.nodes.size(); i++) {
    QCOMPARE(p.nodes[i].x, before.nodes[i].x);
    QCOMPARE(p.nodes[i].y, before.nodes[i].y);
  }
  QCOMPARE(w.windowTitle(), title); // not dirty, so no edit happened
}

void TestMainWindow::fixFromTheMenuHoldsTheSelectedNodesInOneUndoStep()
{
  // Two free nodes and a third left alone. (Fixing a node whose
  // position the sketch already determines is refused as redundant --
  // that is the solver tests' business, not this one's.)
  MainWindow w;
  w.newDocument(FemmProblemKind::Magnetics);
  auto& p = const_cast<FemmProblem&>(w.problem());
  FemmProblemEdit::addNode(p, 0, 0);
  FemmProblemEdit::addNode(p, 10, 0);
  FemmProblemEdit::addNode(p, 20, 5);
  FemmProblemEdit::addSegment(p, 0, 1);
  auto* scene = w.findChild<GeometryScene*>();
  scene->rebuild();
  for (QGraphicsItem* item : scene->items()) {
    if (item->data(0).isValid() && item->data(0).toInt() == static_cast<int>(FemmItemKind::Node)
        && (item->data(1).toInt() == 0 || item->data(1).toInt() == 2))
      item->setSelected(true);
  }
  const int before = w.problem().constraints.size();
  QAction* fix = actionNamed(w, QStringLiteral("&Fix"));
  QVERIFY2(fix, "Constraints > Fix is missing");
  fix->trigger();
  QCOMPARE(w.problem().constraints.size(), before + 2);
  for (int i = before; i < before + 2; i++) {
    const FemmConstraint& c = w.problem().constraints[i];
    QCOMPARE((int)c.type, (int)ConstraintType::Fix);
    QCOMPARE(c.fixX, w.problem().nodes[c.refA].x);
    QCOMPARE(c.fixY, w.problem().nodes[c.refA].y);
  }
  actionNamed(w, QStringLiteral("&Undo"))->trigger();
  QCOMPARE(w.problem().constraints.size(), before);
}

namespace {

// Opens the next modal Area Properties dialog, reads the result for each
// combo index in `indices`, and closes it.
void readAreaDialog(const QList<int>& indices, QStringList& out)
{
  QTimer::singleShot(0, [indices, &out]() {
    QWidget* dlg = QApplication::activeModalWidget();
    if (!dlg)
      return;
    auto* combo = dlg->findChild<QComboBox*>();
    auto* result = dlg->findChild<QLabel*>("areaIntegralResult");
    if (combo && result) {
      for (int i : indices) {
        combo->setCurrentIndex(i);
        out << result->text();
      }
    }
    if (auto* d = qobject_cast<QDialog*>(dlg))
      d->accept();
  });
}

} // namespace

void TestMainWindow::areaPropertiesShowsTheForceOnAWire()
{
  // The demo the review quoted: "Force between two wires... Lorentz and
  // weighted stress tensor" -- both said "Not available". On classic's
  // own solution the left wire feels 0.02467 N (Lorentz) and 0.02466 N
  // (stress tensor) along x; see test/fixtures/integrals/reference.json.
  const QString ans = m_dir.filePath("two_wires_force.ans");
  QFile::remove(ans);
  QVERIFY(QFile::copy(QStringLiteral(FEMMQT_TEST_FIXTURE_DIR "/integrals/two_wires_force.ans"), ans));

  SolutionWindow window;
  QVERIFY(window.openSolutionFile(ans));
  QVERIFY(QMetaObject::invokeMethod(&window, "onAreaToolTriggered"));
  QVERIFY(QMetaObject::invokeMethod(&window, "onCanvasClicked", Q_ARG(QPointF, QPointF(-10, 0))));

  QStringList texts;
  readAreaDialog({ 7, 10, 13, 2 }, texts);
  QVERIFY(QMetaObject::invokeMethod(&window, "onIntegrateTriggered"));
  QCOMPARE(texts.size(), 4);
  QVERIFY2(texts[0].startsWith("50 Amps"), qPrintable(texts[0]));
  QVERIFY2(texts[1].contains("x-component: 0.0246"), qPrintable(texts[1]));
  QVERIFY2(texts[2].contains("x-component: 0.0246"), qPrintable(texts[2]));
  QVERIFY2(texts[3].endsWith("Joules") && !texts[3].contains("Not"), qPrintable(texts[3]));
}

void TestMainWindow::redoBringsBackWhatUndoTookBack()
{
  MainWindow w;
  QVERIFY(w.openFile(copyOf("magnetics/straight_wire.fem")));
  auto* scene = w.findChild<GeometryScene*>();
  auto& p = const_cast<FemmProblem&>(w.problem());
  const int n = p.nodes.size();
  emit scene->aboutToEdit();
  p.nodes.removeLast();

  QAction* undo = actionNamed(w, QStringLiteral("&Undo"));
  QAction* redo = actionNamed(w, QStringLiteral("&Redo"));
  QVERIFY2(redo, "Edit > Redo is missing");
  QVERIFY(redo->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_Y)));
  QVERIFY(redo->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)));

  undo->trigger();
  QCOMPARE(w.problem().nodes.size(), n);
  redo->trigger();
  QCOMPARE(w.problem().nodes.size(), n - 1);
  undo->trigger();
  QCOMPARE(w.problem().nodes.size(), n);

  // A new edit after an undo starts a new future.
  emit scene->aboutToEdit();
  const_cast<FemmProblem&>(w.problem()).nodes.last().x += 1;
  redo->trigger();
  QCOMPARE(w.problem().nodes.size(), n);

  // And opening another document forgets it too (#95's rule, for redo).
  undo->trigger();
  QVERIFY(w.openFile(copyOf("magnetics/coax_cable.fem")));
  const int coax = w.problem().nodes.size();
  redo->trigger();
  QCOMPARE(w.problem().nodes.size(), coax);
}

void TestMainWindow::theUnsavedChangesPromptCanSave()
{
  // Close with unsaved edits: the prompt used to offer only Discard or
  // Cancel. Choosing Save must write the file and then let the window go.
  const QString path = copyOf("magnetics/straight_wire.fem");
  MainWindow w;
  QVERIFY(w.openFile(path));
  auto* scene = w.findChild<GeometryScene*>();
  emit scene->aboutToEdit();
  auto& p = const_cast<FemmProblem&>(w.problem());
  p.nodes.last().x += 0.5;
  const double moved = p.nodes.last().x;
  emit scene->problemEdited(); // marks the document dirty
  QVERIFY(w.windowTitle().contains('*'));

  Notify::setHeadless(false); // a real dialog, answered below
  QStringList buttons;
  QTimer::singleShot(0, [&buttons]() {
    auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    if (!box)
      return;
    for (QAbstractButton* b : box->buttons())
      buttons << b->text();
    if (QAbstractButton* save = box->button(QMessageBox::Save))
      save->click();
    else
      box->reject(); // no Save to press: the defect itself
  });
  const bool closed = w.close();
  Notify::setHeadless(true);

  QVERIFY2(buttons.size() == 3, qPrintable(buttons.join(", ")));
  QVERIFY(closed);
  FemmProblem back;
  QString err;
  QVERIFY2(ProblemFileIO::readAs(path, FemmProblemKind::Magnetics, back, err), qPrintable(err));
  QCOMPARE(back.nodes.last().x, moved);
}

void TestMainWindow::fileDialogsStartWhereTheWorkIs()
{
  // An INI file in the temp dir, not the user's real settings.
  QSettings settings(m_dir.filePath("dialogs.ini"), QSettings::IniFormat);
  settings.clear();
  const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);

  // Nothing known yet: Documents -- never the Start Menu.
  QCOMPARE(FileDialogDirs::startDir("model", QString(), &settings), docs);

  // The open document's own folder wins.
  QDir(m_dir.path()).mkpath("project");
  const QString model = m_dir.filePath("project/motor.fem");
  QCOMPARE(QDir::cleanPath(FileDialogDirs::startDir("model", model, &settings)), QDir::cleanPath(m_dir.filePath("project")));


  // Remembered per kind.
  QDir(m_dir.path()).mkpath("drawings");
  FileDialogDirs::remember("dxf", m_dir.filePath("drawings/part.dxf"), &settings);
  QCOMPARE(QDir::cleanPath(FileDialogDirs::startDir("dxf", QString(), &settings)), QDir::cleanPath(m_dir.filePath("drawings")));
  QCOMPARE(FileDialogDirs::startDir("model", QString(), &settings), docs);
  QCOMPARE(QDir::cleanPath(FileDialogDirs::startPath("dxf", "out.dxf", QString(), &settings)),
      QDir::cleanPath(m_dir.filePath("drawings/out.dxf")));

  // A remembered folder that has gone is skipped, not offered.
  QDir(m_dir.filePath("drawings")).removeRecursively();
  QCOMPARE(FileDialogDirs::startDir("dxf", QString(), &settings), docs);
  // and a cancelled dialog remembers nothing
  FileDialogDirs::remember("dxf", QString(), &settings);
  QCOMPARE(FileDialogDirs::startDir("dxf", QString(), &settings), docs);
}

void TestMainWindow::noFileDialogStartsInTheWorkingDirectory()
{
  // The defect's shape: QFileDialog::get...(parent, title, QString(), ...)
  // opens in the process's working directory -- the Start Menu folder,
  // from the shortcut. Any new dialog written that way fails here.
  QStringList offenders;
  for (const QFileInfo& fi : QDir(QStringLiteral(FEMMQT_SOURCE_DIR)).entryInfoList({ "*.cpp" }, QDir::Files)) {
    QFile f(fi.absoluteFilePath());
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString code = QString::fromUtf8(f.readAll());
    static const QRegularExpression call(QStringLiteral("QFileDialog::get\\w+\\(([^;]*?)\\)\\s*;"),
        QRegularExpression::DotMatchesEverythingOption);
    auto it = call.globalMatch(code);
    while (it.hasNext()) {
      const QString args = it.next().captured(1);
      const QStringList parts = args.split(',');
      if (parts.size() >= 3 && parts[2].trimmed() == QStringLiteral("QString()"))
        offenders << fi.fileName();
    }
  }
  QVERIFY2(offenders.isEmpty(), qPrintable("file dialogs with no start directory in: " + offenders.join(", ")));
}

void TestMainWindow::theInstalledShortcutsStartInDocuments()
{
  QFile f(QStringLiteral(FEMMQT_SOURCE_DIR "/../script.nsi"));
  QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
  const QString nsi = QString::fromUtf8(f.readAll());
  const int link = nsi.indexOf(QStringLiteral("FEMMX (Qt).lnk"));
  QVERIFY(link > 0);
  // NSIS's CreateShortcut takes the current SetOutPath as "Start in":
  // the last SetOutPath before the shortcuts decides it.
  const int lastOut = nsi.lastIndexOf(QStringLiteral("SetOutPath"), link);
  QVERIFY(lastOut > 0);
  const QString line = nsi.mid(lastOut, nsi.indexOf(QLatin1Char('\n'), lastOut) - lastOut);
  QVERIFY2(line.contains(QStringLiteral("$DOCUMENTS")), qPrintable(line));
}

void TestMainWindow::homeStillFitsAfterADialog()
{
  MainWindow w;
  QVERIFY(w.openFile(copyOf("magnetics/straight_wire.fem")));
  w.resize(900, 700);
  w.show();
  QVERIFY(QTest::qWaitForWindowExposed(&w));
  auto* view = w.findChild<GeometryView*>();
  QVERIFY(view);
  view->setFocus();

  // Something modal comes and goes: Problem Properties, answered by Esc.
  QTimer::singleShot(0, []() {
    if (QWidget* d = QApplication::activeModalWidget())
      QMetaObject::invokeMethod(d, "reject", Qt::QueuedConnection);
  });
  QAction* props = actionNamed(w, QStringLiteral("&Problem Properties..."));
  QVERIFY2(props, "no Problem properties action");
  props->trigger();
  QApplication::processEvents();

  // Zoom far in, then press Home on the canvas: it must fit again.
  QApplication::setActiveWindow(&w);
  view->setFocus();
  QVERIFY(QTest::qWaitForWindowActive(&w));
  actionNamed(w, QStringLiteral("&Natural"))->trigger();
  const double fit = view->transform().m11();
  view->zoomBy(16);
  QTest::keyClick(view, Qt::Key_Home);
  QApplication::processEvents();
  QVERIFY2(std::abs(view->transform().m11() - fit) < 1e-6 * fit,
      qPrintable(QStringLiteral("Home did not fit: scale %1, the fit is %2").arg(view->transform().m11()).arg(fit)));
}

void TestMainWindow::theGridIsOnButNotInRenders()
{
  MainWindow w;
  QVERIFY(w.openFile(copyOf("magnetics/straight_wire.fem")));
  auto* scene = w.findChild<GeometryScene*>();
  QVERIFY2(scene->showGrid(), "the grid is off by default");
  const QImage withGridOn = w.renderToImage(QSize(300, 300));
  scene->setShowGrid(false);
  const QImage withGridOff = w.renderToImage(QSize(300, 300));
  QVERIFY(!withGridOn.isNull());
  QCOMPARE(withGridOn, withGridOff); // a render shows the model only
  scene->setShowGrid(true);
  w.renderToImage(QSize(50, 50));
  QVERIFY2(scene->showGrid(), "rendering turned the user's grid off");
}

void TestMainWindow::zoomStepsAreGentleAndFitLeavesAMargin()
{
  MainWindow w;
  QVERIFY(w.openFile(copyOf("magnetics/straight_wire.fem")));
  w.resize(900, 700);
  w.show();
  QVERIFY(QTest::qWaitForWindowExposed(&w));
  auto* view = w.findChild<GeometryView*>();
  auto* scene = w.findChild<GeometryScene*>();
  actionNamed(w, QStringLiteral("&Natural"))->trigger();
  const double fit = view->transform().m11();
  actionNamed(w, QStringLiteral("Zoom &Out"))->trigger();
  QVERIFY(std::abs(view->transform().m11() - fit / M_SQRT2) < 1e-9 * fit);

  // After a fit, the model stays clear of every edge of the viewport.
  actionNamed(w, QStringLiteral("&Natural"))->trigger();
  const QRect onScreen = view->mapFromScene(scene->computeProblemBounds()).boundingRect();
  const QRect vp = view->viewport()->rect();
  QVERIFY2(vp.adjusted(4, 4, -4, -4).contains(onScreen),
      qPrintable(QStringLiteral("model %1,%2 %3x%4 in viewport %5x%6")
                     .arg(onScreen.x()).arg(onScreen.y()).arg(onScreen.width()).arg(onScreen.height())
                     .arg(vp.width()).arg(vp.height())));
}

void TestMainWindow::aUniformFieldPaintsOneColourNotNoise()
{
  // The review's case: the parallel-plate capacitor's |D| is uniform,
  // 1.771e-7 everywhere to round-off, and painted as a rainbow of noise.
  FemmProblem problem;
  SolvedMesh solved;
  QString err;
  QVERIFY2(SolutionFileIO::read(QStringLiteral(FEMMQT_TEST_FIXTURE_DIR "/solutions/parallel_plate.res"), problem, solved, err),
      qPrintable(err));
  MeshSolution sol;
  QVERIFY(SolutionAdapter::toMeshSolution(solved, problem, sol));
  MeshSolutionItem item(&sol);
  item.setProblemKind(FemmProblemKind::Electrostatics);
  item.setPlotMode(MeshSolutionItem::PlotMode::Density);
  item.setSmoothing(false);

  // Only the gap between the plates, where the field is uniform.
  double lo = 0, hi = 0;
  QGraphicsScene scene;
  scene.addItem(&item);
  QImage img(300, 300, QImage::Format_RGB32);
  img.fill(Qt::white);
  QRectF all = item.boundingRect();
  const QRectF gap(all.center().x() - all.width() * 0.05, all.center().y() - all.height() * 0.05,
      all.width() * 0.1, all.height() * 0.1);
  {
    QPainter painter(&img);
    scene.render(&painter, QRectF(img.rect()), gap);
  }
  item.legendRange(lo, hi);
  scene.removeItem(&item);
  QVERIFY2(hi - lo > 1e-3 * std::abs(hi),
      qPrintable(QStringLiteral("the legend spans %1 .. %2 -- round-off").arg(lo, 0, 'g', 12).arg(hi, 0, 'g', 12)));
  QSet<QRgb> colours;
  for (int y = 20; y < img.height() - 20; y++)
    for (int x = 20; x < img.width() - 20; x++)
      colours.insert(img.pixel(x, y));
  QVERIFY2(colours.size() <= 3, qPrintable(QStringLiteral("%1 colours in a uniform field").arg(colours.size())));
}

void TestMainWindow::pointPropertiesIsAPanelNotAModalPerClick()
{
  const QString ans = m_dir.filePath("pp_wire.ans");
  QFile::remove(ans);
  QVERIFY(QFile::copy(QStringLiteral(FEMMQT_TEST_FIXTURE_DIR "/integrals/straight_wire.ans"), ans));
  SolutionWindow window;
  QVERIFY(window.openSolutionFile(ans));

  // Any modal that appears is the defect: close it and remember.
  bool sawModal = false;
  QTimer guard;
  connect(&guard, &QTimer::timeout, [&sawModal]() {
    if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
      sawModal = true;
      d->reject();
    }
  });
  guard.start(0);
  QVERIFY(QMetaObject::invokeMethod(&window, "onPointToolTriggered"));
  QVERIFY(QMetaObject::invokeMethod(&window, "onCanvasClicked", Q_ARG(QPointF, QPointF(5, 0))));
  QVERIFY(QMetaObject::invokeMethod(&window, "onCanvasClicked", Q_ARG(QPointF, QPointF(6, 0))));
  guard.stop();
  QVERIFY2(!sawModal, "a click opened a modal dialog");
  auto* dock = window.findChild<QDockWidget*>("pointPropertiesDock");
  QVERIFY2(dock && dock->isVisibleTo(&window), "no Point Properties panel");
  QVERIFY(dock->widget()->findChildren<QLabel*>().size() > 4);

  // The output carries the clicks -- until another solution opens.
  auto* output = window.findChild<QPlainTextEdit*>();
  QVERIFY(output && output->toPlainText().contains("Point:"));
  QVERIFY(window.openSolutionFile(ans));
  QVERIFY2(output->toPlainText().isEmpty(), "the previous solution's output is still showing");
  QVERIFY(!dock->isVisibleTo(&window));

  // Plot X-Y and Integrate live in a menu, not bare on the menu bar.
  for (QAction* a : window.menuBar()->actions())
    QVERIFY2(a->menu() != nullptr, qPrintable(QStringLiteral("bare menu-bar action: %1").arg(a->text())));
}

void TestMainWindow::theSolutionWindowRefitsOnResizeUntilTheUserZooms()
{
  const QString ans = m_dir.filePath("fit_wire.ans");
  QFile::remove(ans);
  QVERIFY(QFile::copy(QStringLiteral(FEMMQT_TEST_FIXTURE_DIR "/integrals/straight_wire.ans"), ans));
  SolutionWindow window;
  window.resize(700, 500);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  QVERIFY(window.openSolutionFile(ans));
  auto* view = window.findChild<SolutionGraphicsView*>();
  QVERIFY(view);
  const double small = view->transform().m11();
  window.resize(1400, 1000); // what maximising does
  QApplication::processEvents();
  const double big = view->transform().m11();
  QVERIFY2(big > small * 1.3, qPrintable(QStringLiteral("not refitted: %1 -> %2").arg(small).arg(big)));

  // After the user zooms, a resize leaves their view alone.
  QVERIFY(QMetaObject::invokeMethod(&window, "onZoomIn"));
  const double zoomed = view->transform().m11();
  window.resize(900, 700);
  QApplication::processEvents();
  QCOMPARE(view->transform().m11(), zoomed);
}

void TestMainWindow::anAreaSelectionLetsThePlotShowThrough()
{
  FemmProblem p;
  MeshSolution sol;
  QString err;
  QVERIFY2(AnsFileIO::readAns(QStringLiteral(FEMMQT_TEST_FIXTURE_DIR "/integrals/straight_wire.ans"), p, sol, err), qPrintable(err));
  MeshSolutionItem item(&sol);
  item.setProblemGeometry(&p);
  item.setPlotMode(MeshSolutionItem::PlotMode::Density);
  const QImage plain = renderSolution(item, sol);
  item.toggleBlockLabelSelected(1); // the air around the wire: most of the picture
  const QImage selected = renderSolution(item, sol);
  // Opaque, the whole air region became one flat green. Translucent,
  // the density bands still differ from one another underneath.
  QSet<QRgb> colours;
  for (int y = 0; y < selected.height(); y += 3)
    for (int x = 0; x < selected.width(); x += 3)
      colours.insert(selected.pixel(x, y));
  QSet<QRgb> plainColours;
  for (int y = 0; y < plain.height(); y += 3)
    for (int x = 0; x < plain.width(); x += 3)
      plainColours.insert(plain.pixel(x, y));
  QVERIFY2(colours.size() > plainColours.size() / 2,
      qPrintable(QStringLiteral("%1 colours with the air selected, %2 without").arg(colours.size()).arg(plainColours.size())));
}

QTEST_MAIN(TestMainWindow)
#include "tst_main_window.moc"
