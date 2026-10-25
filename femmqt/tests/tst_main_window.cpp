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
#include <QComboBox>
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
#include "AppTheme.h"
#include "ConstraintSolver.h"
#include "FemmProblemEdit.h"
#include "ProblemFileIO.h"
#include "SketchFileIO.h"
#include "BlockLabelPropDialog.h"
#include "SolutionView.h"
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

QTEST_MAIN(TestMainWindow)
#include "tst_main_window.moc"
