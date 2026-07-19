#include "MainWindow.h"

#include "ArcPropDialog.h"
#include "BlockLabelPropDialog.h"
#include "BoundaryPropDialog.h"
#include "CircuitPropDialog.h"
#include "FemmFileIO.h"
#include "FemmProblemEdit.h"
#include "FemxFileIO.h"
#include "GuiSwitch.h"
#include "IconTheme.h"
#include "MaterialPropDialog.h"
#include "NodePropDialog.h"
#include "PointPropDialog.h"
#include "ProblemPropertiesDialog.h"
#include "PropertyListDialog.h"
#include "SegmentPropDialog.h"
#include "SolutionView.h"
#include "SolveRunner.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsView>
#include <QMenuBar>
#include <QMessageBox>
#include <QSet>
#include <QStatusBar>
#include <QToolBar>

namespace {
// Generates "New <Base>"/"<Base> Copy", disambiguated with a trailing
// " (2)", " (3)", ... against whatever names are already in `list` --
// shared by the Add New / Duplicate callbacks below for all four
// property lists (they're otherwise unrelated struct types, but all
// start with a QString `name` member, so this only needs to be a small
// template rather than four near-identical copies).
template <typename T>
QString uniqueName(const QVector<T>& list, const QString& base)
{
  QSet<QString> existing;
  for (const T& item : list)
    existing.insert(item.name);
  if (!existing.contains(base))
    return base;
  for (int n = 2;; n++) {
    QString candidate = QString("%1 (%2)").arg(base).arg(n);
    if (!existing.contains(candidate))
      return candidate;
  }
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
  resize(1024, 768);

  m_scene = new GeometryScene(this);
  m_scene->setProblem(&m_problem);
  connect(m_scene, &GeometryScene::problemEdited, this, &MainWindow::onProblemEdited);
  connect(m_scene, &GeometryScene::entityDoubleClicked, this, &MainWindow::onEntityDoubleClicked);

  m_view = new QGraphicsView(m_scene, this);
  m_view->setRenderHint(QPainter::Antialiasing);
  m_view->setDragMode(QGraphicsView::RubberBandDrag);
  // FEMM's geometry files use a math (y-up) convention, matching how
  // femmx.exe itself draws (see FemmeView.cpp's DwgToScreen, which flips
  // y explicitly for GDI's y-down screen space) -- flip the view instead
  // of the data, so GeometryScene's coordinates can stay exactly what's
  // in the .fem file with no transform bookkeeping of its own.
  m_view->scale(1, -1);
  setCentralWidget(m_view);

  QMenu* fileMenu = menuBar()->addMenu("&File");
  fileMenu->addAction("&New", this, &MainWindow::onNewTriggered, QKeySequence::New);
  fileMenu->addAction("&Open...", this, &MainWindow::onOpenTriggered, QKeySequence::Open);
  fileMenu->addAction("&Save", this, &MainWindow::onSaveTriggered, QKeySequence::Save);
  fileMenu->addAction("Save &As...", this, &MainWindow::onSaveAsTriggered, QKeySequence::SaveAs);
  fileMenu->addSeparator();
  fileMenu->addAction("Switch to &Classic GUI...", this, &MainWindow::onSwitchToClassicTriggered);
  fileMenu->addSeparator();
  fileMenu->addAction("E&xit", this, &QWidget::close);

  QMenu* meshMenu = menuBar()->addMenu("&Mesh");
  meshMenu->addAction("&Solve", this, &MainWindow::onSolveTriggered, QKeySequence("Ctrl+L"));
  meshMenu->addAction("&View Results...", this, &MainWindow::onViewResultsTriggered);

  QMenu* problemMenu = menuBar()->addMenu("&Problem");
  problemMenu->addAction("&Problem Properties...", this, &MainWindow::onProblemPropertiesTriggered);
  problemMenu->addSeparator();
  problemMenu->addAction("&Materials...", this, &MainWindow::onMaterialsTriggered);
  problemMenu->addAction("&Boundary Properties...", this, &MainWindow::onBoundaryPropsTriggered);
  problemMenu->addAction("&Circuits...", this, &MainWindow::onCircuitsTriggered);
  problemMenu->addAction("Poi&nt Properties...", this, &MainWindow::onPointPropsTriggered);

  QToolBar* toolBar = addToolBar("Draw");
  // Icon-only, matching a modern CAD-style toolbar -- each action keeps
  // its descriptive text (set below) as a tooltip and for the
  // accessibility/menu fallback, it just isn't painted next to the icon.
  toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
  toolBar->setIconSize(QSize(20, 20));
  auto* toolGroup = new QActionGroup(this);
  toolGroup->setExclusive(true);

  m_selectToolAction = toolBar->addAction(IconTheme::themedToolIcon(":/icons/select.svg"), "Select");
  m_selectToolAction->setCheckable(true);
  m_selectToolAction->setChecked(true);
  toolGroup->addAction(m_selectToolAction);
  connect(m_selectToolAction, &QAction::triggered, this, [this]() { m_scene->setToolMode(GeometryToolMode::Select); });

  m_addNodeToolAction = toolBar->addAction(IconTheme::themedToolIcon(":/icons/add_node.svg"), "Add Node");
  m_addNodeToolAction->setCheckable(true);
  toolGroup->addAction(m_addNodeToolAction);
  connect(m_addNodeToolAction, &QAction::triggered, this, [this]() { m_scene->setToolMode(GeometryToolMode::AddNode); });

  m_addSegmentToolAction = toolBar->addAction(IconTheme::themedToolIcon(":/icons/add_segment.svg"), "Add Segment");
  m_addSegmentToolAction->setCheckable(true);
  toolGroup->addAction(m_addSegmentToolAction);
  connect(m_addSegmentToolAction, &QAction::triggered, this, [this]() { m_scene->setToolMode(GeometryToolMode::AddSegment); });

  m_addArcToolAction = toolBar->addAction(IconTheme::themedToolIcon(":/icons/add_arc.svg"), "Add Arc");
  m_addArcToolAction->setCheckable(true);
  toolGroup->addAction(m_addArcToolAction);
  connect(m_addArcToolAction, &QAction::triggered, this, [this]() { m_scene->setToolMode(GeometryToolMode::AddArc); });

  m_addBlockLabelToolAction = toolBar->addAction(IconTheme::themedToolIcon(":/icons/add_block_label.svg"), "Add Block Label");
  m_addBlockLabelToolAction->setCheckable(true);
  toolGroup->addAction(m_addBlockLabelToolAction);
  connect(m_addBlockLabelToolAction, &QAction::triggered, this, [this]() { m_scene->setToolMode(GeometryToolMode::AddBlockLabel); });

  statusBar()->showMessage("Ready");
  updateTitle();
}

void MainWindow::onNewTriggered()
{
  if (!confirmDiscardUnsavedChanges())
    return;
  m_problem = FemmProblem();
  m_currentPath.clear();
  m_scene->setProblem(&m_problem);
  m_dirty = false; // after setProblem() -- see the matching comment in openFile()
  m_view->resetTransform();
  m_view->scale(1, -1);
  updateTitle();
  statusBar()->showMessage("New problem");
}

void MainWindow::onOpenTriggered()
{
  if (!confirmDiscardUnsavedChanges())
    return;
  QString path = QFileDialog::getOpenFileName(this, "Open Magnetics Problem", QString(), "FEMM Magnetics Files (*.fem)");
  if (path.isEmpty())
    return;
  openFile(path);
}

void MainWindow::openFile(const QString& path)
{
  QFileInfo pathInfo(path);
  QString femPath = path;
  QString femxPath = pathInfo.absolutePath() + "/" + pathInfo.completeBaseName() + ".femx";
  if (pathInfo.suffix().compare("femx", Qt::CaseInsensitive) == 0) {
    femPath = pathInfo.absolutePath() + "/" + pathInfo.completeBaseName() + ".fem";
    femxPath = path;
  }

  QString error;
  FemmProblem problem;
  bool loadedFromFemx = false;

  if (FemxFileIO::isUpToDate(femxPath, femPath)) {
    if (FemxFileIO::readFemx(femxPath, problem, error))
      loadedFromFemx = true;
    // falls through to the .fem path below if the cache turned out corrupt
  }

  if (!loadedFromFemx) {
    if (!QFileInfo::exists(femPath)) {
      QMessageBox::warning(this, "Open Failed",
          QStringLiteral("\"%1\" doesn't exist and no matching .femx cache was found.").arg(femPath));
      return;
    }
    if (!FemmFileIO::readFem(femPath, problem, error)) {
      QMessageBox::warning(this, "Open Failed", error);
      return;
    }
    // Cache for next time -- best-effort, same as the .ansx side.
    QString writeError;
    FemxFileIO::writeFemx(femxPath, femPath, problem, writeError);
  }

  m_problem = problem;
  m_currentPath = femPath;
  m_scene->setProblem(&m_problem);
  m_view->fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
  // After, not before, setProblem(): populating the scene calls setPos()
  // on every new NodeItem, which -- same as a real user drag -- fires
  // ItemPositionHasChanged -> onNodeMoved() -> problemEdited(), so a
  // freshly-opened, untouched file would otherwise show as dirty
  // immediately (confirmed directly: the window title showed a
  // trailing "*" right after this session's first GUI-switch test,
  // before this fix).
  m_dirty = false;

  statusBar()->showMessage(QString("%1 -- %2 nodes, %3 segments, %4 arcs, %5 block labels (loaded via %6)")
                                .arg(femPath)
                                .arg(m_problem.nodes.size())
                                .arg(m_problem.segments.size())
                                .arg(m_problem.arcSegments.size())
                                .arg(m_problem.blockLabels.size())
                                .arg(loadedFromFemx ? ".femx" : ".fem"));
  updateTitle();
}

void MainWindow::onSaveTriggered()
{
  if (m_currentPath.isEmpty()) {
    onSaveAsTriggered();
    return;
  }
  saveAs(m_currentPath);
}

void MainWindow::onSaveAsTriggered()
{
  QString path = QFileDialog::getSaveFileName(this, "Save Magnetics Problem", m_currentPath, "FEMM Magnetics Files (*.fem)");
  if (path.isEmpty())
    return;
  saveAs(path);
}

bool MainWindow::saveAs(const QString& path)
{
  QString error;
  if (!FemmFileIO::writeFem(path, m_problem, error)) {
    QMessageBox::warning(this, "Save Failed", error);
    return false;
  }
  // Best-effort .femx cache refresh -- unlike .ansx's lazy on-open
  // generation, this can be done right here since the just-saved
  // FemmProblem is already in hand, so a save never leaves a stale cache
  // behind for this session's own next open.
  QFileInfo pathInfo(path);
  QString femxPath = pathInfo.absolutePath() + "/" + pathInfo.completeBaseName() + ".femx";
  QString femxError;
  FemxFileIO::writeFemx(femxPath, path, m_problem, femxError);

  m_currentPath = path;
  m_dirty = false;
  updateTitle();
  statusBar()->showMessage(QString("Saved %1").arg(path));
  return true;
}

void MainWindow::onSolveTriggered()
{
  // OnWritePoly (see MeshBuilder's header comment) reads geometry from
  // the saved .fem on disk, not straight from m_problem in memory --
  // matching the classic GUI's own OnMenuAnalyze (femm/FemmeView.cpp:
  // 2743), which force-saves right before meshing/solving. A file must
  // exist on disk either way, so this always saves first rather than
  // just checking m_dirty.
  if (m_currentPath.isEmpty()) {
    onSaveAsTriggered();
    if (m_currentPath.isEmpty())
      return; // user cancelled the save dialog
  } else if (!saveAs(m_currentPath)) {
    return;
  }

  if (hasAppliedPeriodicBoundary()) {
    QMessageBox::warning(this, "Cannot Solve",
        "This problem uses a periodic or antiperiodic boundary condition, "
        "which this Qt GUI doesn't support meshing for yet. Open it in the "
        "classic FEMMX GUI to solve it.");
    return;
  }

  statusBar()->showMessage("Meshing and solving...");
  QApplication::setOverrideCursor(Qt::WaitCursor);
  QString error;
  bool ok = SolveRunner::solve(m_problem, m_currentPath, error);
  QApplication::restoreOverrideCursor();

  if (!ok) {
    statusBar()->showMessage("Solve failed");
    QMessageBox::warning(this, "Solve Failed", error);
    return;
  }

  QString ansPath = QFileInfo(m_currentPath).absolutePath() + "/" + QFileInfo(m_currentPath).completeBaseName() + ".ans";
  statusBar()->showMessage(QString("Solved -- see %1").arg(ansPath));

  if (!m_solutionWindow)
    m_solutionWindow = new SolutionWindow();
  m_solutionWindow->openAnsFile(ansPath);
  m_solutionWindow->show();
  m_solutionWindow->raise();
  m_solutionWindow->activateWindow();
}

void MainWindow::onViewResultsTriggered()
{
  if (!m_solutionWindow)
    m_solutionWindow = new SolutionWindow();
  m_solutionWindow->show();
  m_solutionWindow->raise();
  m_solutionWindow->activateWindow();
}

void MainWindow::onSwitchToClassicTriggered()
{
  // The other GUI needs a file on disk to open (there's no in-memory
  // handoff between two separate processes) -- same "must be saved"
  // requirement as onSolveTriggered, reusing the same confirm-or-save
  // flow the normal close path already uses.
  if (m_dirty) {
    auto result = QMessageBox::question(this, "Switch GUI",
        "Save changes before switching to the classic GUI?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (result == QMessageBox::Cancel)
      return;
    if (result == QMessageBox::Save) {
      if (m_currentPath.isEmpty()) {
        onSaveAsTriggered();
        if (m_currentPath.isEmpty() || m_dirty)
          return; // save dialog was cancelled, or failed
      } else if (!saveAs(m_currentPath)) {
        return;
      }
    }
  }

  GuiSwitch::writePreferredGui(GuiSwitch::PreferredGui::Classic);
  if (!GuiSwitch::launchClassicGui(m_currentPath)) {
    QMessageBox::warning(this, "Switch Failed",
        "Couldn't find or start femmx.exe next to femmqt.exe.");
    return;
  }
  close();
}

bool MainWindow::hasAppliedPeriodicBoundary() const
{
  // Mirrors CFemmeDoc::HasPeriodicBC (femm/writepoly.cpp:28-88): BdryFormat
  // 4-7 are the periodic/antiperiodic variants: 4/5 are the ones a plain
  // segment can carry (femm/writepoly.cpp:57), while arcs can carry any of
  // 4-7 (femm/writepoly.cpp:77). Only checks properties actually referenced
  // by a segment/arc, not just defined -- an unused periodic boundary
  // definition doesn't block solving, matching the original's own
  // two-phase check.
  auto isPeriodicFormat = [](int fmt, bool allowExtended) {
    return fmt == 4 || fmt == 5 || (allowExtended && (fmt == 6 || fmt == 7));
  };
  for (const FemmSegment& s : m_problem.segments) {
    if (s.boundaryMarker <= 0 || s.boundaryMarker > m_problem.boundaryProps.size())
      continue;
    if (isPeriodicFormat(m_problem.boundaryProps[s.boundaryMarker - 1].bdryFormat, false))
      return true;
  }
  for (const FemmArcSegment& a : m_problem.arcSegments) {
    if (a.boundaryMarker <= 0 || a.boundaryMarker > m_problem.boundaryProps.size())
      continue;
    if (isPeriodicFormat(m_problem.boundaryProps[a.boundaryMarker - 1].bdryFormat, true))
      return true;
  }
  return false;
}

void MainWindow::onProblemEdited()
{
  markEdited();
}

void MainWindow::markEdited()
{
  m_dirty = true;
  updateTitle();
}

void MainWindow::onProblemPropertiesTriggered()
{
  ProblemPropertiesDialog dlg(m_problem, this);
  if (dlg.exec() == QDialog::Accepted)
    markEdited();
}

void MainWindow::onMaterialsTriggered()
{
  PropertyListDialog::Callbacks cb;
  cb.count = [this]() { return m_problem.materialProps.size(); };
  cb.nameAt = [this](int i) { return m_problem.materialProps[i].name; };
  cb.editAt = [this](int i) {
    MaterialPropDialog dlg(m_problem.materialProps[i], this);
    if (dlg.exec() == QDialog::Accepted)
      markEdited();
  };
  cb.addNew = [this]() {
    FemmMaterialProp m;
    m.name = uniqueName(m_problem.materialProps, "New Material");
    m_problem.materialProps.push_back(m);
    markEdited();
  };
  cb.duplicate = [this](int i) {
    FemmMaterialProp m = m_problem.materialProps[i];
    m.name = uniqueName(m_problem.materialProps, m.name);
    m_problem.materialProps.push_back(m);
    markEdited();
  };
  cb.referenceCount = [this](int i) { return FemmProblemEdit::countMaterialPropReferences(m_problem, i); };
  cb.remove = [this](int i) {
    FemmProblemEdit::deleteMaterialProp(m_problem, i);
    m_scene->rebuild();
    markEdited();
  };
  PropertyListDialog dlg("Materials", "material", cb, this);
  dlg.exec();
}

void MainWindow::onBoundaryPropsTriggered()
{
  PropertyListDialog::Callbacks cb;
  cb.count = [this]() { return m_problem.boundaryProps.size(); };
  cb.nameAt = [this](int i) { return m_problem.boundaryProps[i].name; };
  cb.editAt = [this](int i) {
    BoundaryPropDialog dlg(m_problem.boundaryProps[i], this);
    if (dlg.exec() == QDialog::Accepted)
      markEdited();
  };
  cb.addNew = [this]() {
    FemmBoundaryProp b;
    b.name = uniqueName(m_problem.boundaryProps, "New Boundary");
    m_problem.boundaryProps.push_back(b);
    markEdited();
  };
  cb.duplicate = [this](int i) {
    FemmBoundaryProp b = m_problem.boundaryProps[i];
    b.name = uniqueName(m_problem.boundaryProps, b.name);
    m_problem.boundaryProps.push_back(b);
    markEdited();
  };
  cb.referenceCount = [this](int i) { return FemmProblemEdit::countBoundaryPropReferences(m_problem, i); };
  cb.remove = [this](int i) {
    FemmProblemEdit::deleteBoundaryProp(m_problem, i);
    m_scene->rebuild();
    markEdited();
  };
  PropertyListDialog dlg("Boundary Properties", "boundary", cb, this);
  dlg.exec();
}

void MainWindow::onCircuitsTriggered()
{
  PropertyListDialog::Callbacks cb;
  cb.count = [this]() { return m_problem.circuitProps.size(); };
  cb.nameAt = [this](int i) { return m_problem.circuitProps[i].name; };
  cb.editAt = [this](int i) {
    CircuitPropDialog dlg(m_problem.circuitProps[i], this);
    if (dlg.exec() == QDialog::Accepted)
      markEdited();
  };
  cb.addNew = [this]() {
    FemmCircuitProp c;
    c.name = uniqueName(m_problem.circuitProps, "New Circuit");
    m_problem.circuitProps.push_back(c);
    markEdited();
  };
  cb.duplicate = [this](int i) {
    FemmCircuitProp c = m_problem.circuitProps[i];
    c.name = uniqueName(m_problem.circuitProps, c.name);
    m_problem.circuitProps.push_back(c);
    markEdited();
  };
  cb.referenceCount = [this](int i) { return FemmProblemEdit::countCircuitPropReferences(m_problem, i); };
  cb.remove = [this](int i) {
    FemmProblemEdit::deleteCircuitProp(m_problem, i);
    m_scene->rebuild();
    markEdited();
  };
  PropertyListDialog dlg("Circuits", "circuit", cb, this);
  dlg.exec();
}

void MainWindow::onPointPropsTriggered()
{
  PropertyListDialog::Callbacks cb;
  cb.count = [this]() { return m_problem.pointProps.size(); };
  cb.nameAt = [this](int i) { return m_problem.pointProps[i].name; };
  cb.editAt = [this](int i) {
    PointPropDialog dlg(m_problem.pointProps[i], this);
    if (dlg.exec() == QDialog::Accepted)
      markEdited();
  };
  cb.addNew = [this]() {
    FemmPointProp p;
    p.name = uniqueName(m_problem.pointProps, "New Point Property");
    m_problem.pointProps.push_back(p);
    markEdited();
  };
  cb.duplicate = [this](int i) {
    FemmPointProp p = m_problem.pointProps[i];
    p.name = uniqueName(m_problem.pointProps, p.name);
    m_problem.pointProps.push_back(p);
    markEdited();
  };
  cb.referenceCount = [this](int i) { return FemmProblemEdit::countPointPropReferences(m_problem, i); };
  cb.remove = [this](int i) {
    FemmProblemEdit::deletePointProp(m_problem, i);
    m_scene->rebuild();
    markEdited();
  };
  PropertyListDialog dlg("Point Properties", "point property", cb, this);
  dlg.exec();
}

void MainWindow::onEntityDoubleClicked(FemmItemKind kind, int index)
{
  bool accepted = false;
  switch (kind) {
  case FemmItemKind::Node:
    if (index >= 0 && index < m_problem.nodes.size()) {
      NodePropDialog dlg(m_problem.nodes[index], m_problem, this);
      accepted = dlg.exec() == QDialog::Accepted;
    }
    break;
  case FemmItemKind::Segment:
    if (index >= 0 && index < m_problem.segments.size()) {
      SegmentPropDialog dlg(m_problem.segments[index], m_problem, this);
      accepted = dlg.exec() == QDialog::Accepted;
    }
    break;
  case FemmItemKind::Arc:
    if (index >= 0 && index < m_problem.arcSegments.size()) {
      ArcPropDialog dlg(m_problem.arcSegments[index], m_problem, this);
      accepted = dlg.exec() == QDialog::Accepted;
    }
    break;
  case FemmItemKind::BlockLabel:
    if (index >= 0 && index < m_problem.blockLabels.size()) {
      BlockLabelPropDialog dlg(m_problem.blockLabels[index], m_problem, this);
      accepted = dlg.exec() == QDialog::Accepted;
    }
    break;
  }

  if (accepted) {
    // A block label's hole/material-assigned rendering (see
    // addBlockLabelItem's pen color) depends on blockTypeIndex, which
    // this dialog can change -- rebuild() to reflect it. Cheap enough at
    // this editor's scale (thousands, not millions, of entities) to just
    // always do it here rather than tracking which edits actually need it.
    m_scene->rebuild();
    markEdited();
  }
}

void MainWindow::updateTitle()
{
  QString name = m_currentPath.isEmpty() ? QStringLiteral("Untitled") : m_currentPath;
  setWindowTitle(QString("FEMMX (Qt) - Magnetics - %1%2").arg(name, m_dirty ? "*" : ""));
}

bool MainWindow::confirmDiscardUnsavedChanges()
{
  if (!m_dirty)
    return true;
  auto result = QMessageBox::question(this, "Unsaved Changes",
      "The current problem has unsaved changes. Discard them?",
      QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
  return result == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
  if (confirmDiscardUnsavedChanges())
    event->accept();
  else
    event->ignore();
}
