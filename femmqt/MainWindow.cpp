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
#include "MeshOverlay.h"
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
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsView>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

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
  connect(m_scene, &GeometryScene::zoomWindowSelected, this, &MainWindow::onZoomWindowSelected);
  connect(m_scene, &GeometryScene::aboutToEdit, this, &MainWindow::snapshotForUndo);

  m_view = new GeometryView(m_scene, this);
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
  m_recentFilesMenu = fileMenu->addMenu("Recent Files");
  fileMenu->addSeparator();
  fileMenu->addAction("Switch to &Classic GUI...", this, &MainWindow::onSwitchToClassicTriggered);
  fileMenu->addSeparator();
  fileMenu->addAction("E&xit", this, &QWidget::close);

  // Matches femm.rc's IDR_FEMMETYPE Edit menu's geometry-transform subset
  // (Undo, Move, Copy, Scale, Mirror) -- Create Radius/Create Open
  // Boundary/Preferences/Copy as Bitmap-Metafile are separate todo items.
  QMenu* editMenu = menuBar()->addMenu("&Edit");
  editMenu->addAction("&Undo", this, &MainWindow::onUndoTriggered, QKeySequence::Undo);
  editMenu->addSeparator();
  editMenu->addAction("&Move...", this, &MainWindow::onMoveSelectedTriggered);
  editMenu->addAction("&Copy...", this, &MainWindow::onCopySelectedTriggered);
  editMenu->addAction("Sc&ale...", this, &MainWindow::onScaleSelectedTriggered);
  editMenu->addAction("M&irror...", this, &MainWindow::onMirrorSelectedTriggered);
  editMenu->addSeparator();
  // Simplified, instant-action equivalent of femm.rc's "Operation > Group"
  // mode (which requires switching modes, then pressing Tab to prompt for
  // a group number) -- same underlying inGroup field, just two direct
  // commands instead of a persistent tool mode.
  editMenu->addAction("Select by &Group...", this, &MainWindow::onSelectByGroupTriggered);
  editMenu->addAction("Set &Group...", this, &MainWindow::onSetGroupTriggered);

  // Matches femm.rc's separate Mesh (Create/Show/Purge) and Analysis
  // (Analyze/View Results) menus, just combined under one "Mesh" menu
  // rather than two -- Create Mesh alone (no solve) lets a mesh be
  // inspected/refined before committing to a full Solve.
  QMenu* meshMenu = menuBar()->addMenu("&Mesh");
  meshMenu->addAction("&Create Mesh", this, &MainWindow::onCreateMeshTriggered);
  m_showMeshAction = meshMenu->addAction("&Show Mesh");
  m_showMeshAction->setCheckable(true);
  connect(m_showMeshAction, &QAction::toggled, m_scene, &GeometryScene::setShowMesh);
  meshMenu->addAction("&Purge Mesh", this, &MainWindow::onPurgeMeshTriggered);
  meshMenu->addSeparator();
  meshMenu->addAction("&Solve", this, &MainWindow::onSolveTriggered, QKeySequence("Ctrl+L"));
  meshMenu->addAction("&View Results...", this, &MainWindow::onViewResultsTriggered);

  QMenu* problemMenu = menuBar()->addMenu("&Problem");
  problemMenu->addAction("&Problem Properties...", this, &MainWindow::onProblemPropertiesTriggered);
  problemMenu->addSeparator();
  problemMenu->addAction("&Materials...", this, &MainWindow::onMaterialsTriggered);
  problemMenu->addAction("&Boundary Properties...", this, &MainWindow::onBoundaryPropsTriggered);
  problemMenu->addAction("&Circuits...", this, &MainWindow::onCircuitsTriggered);
  problemMenu->addAction("Poi&nt Properties...", this, &MainWindow::onPointPropsTriggered);

  // Matches femm.rc's IDR_FEMMETYPE View menu (Zoom/Pan/Show Block Names
  // subset -- Show Orphans/Lua Console/Load Monitor are separate todo
  // items, not part of this group).
  QMenu* viewMenu = menuBar()->addMenu("&View");
  viewMenu->addAction("Zoom &In", this, &MainWindow::onZoomIn, QKeySequence(Qt::Key_PageUp));
  viewMenu->addAction("Zoom &Out", this, &MainWindow::onZoomOut, QKeySequence(Qt::Key_PageDown));
  viewMenu->addAction("&Natural", this, &MainWindow::onZoomNatural, QKeySequence(Qt::Key_Home));
  viewMenu->addAction("&Window", this, &MainWindow::onZoomWindowTriggered);
  viewMenu->addSeparator();
  viewMenu->addAction("Scroll &Left", this, &MainWindow::onPanLeft, QKeySequence(Qt::Key_Left));
  viewMenu->addAction("Scroll &Right", this, &MainWindow::onPanRight, QKeySequence(Qt::Key_Right));
  viewMenu->addAction("Scroll &Up", this, &MainWindow::onPanUp, QKeySequence(Qt::Key_Up));
  viewMenu->addAction("Scroll &Down", this, &MainWindow::onPanDown, QKeySequence(Qt::Key_Down));
  viewMenu->addSeparator();
  QAction* showNamesAction = viewMenu->addAction("Show &Block Names");
  showNamesAction->setCheckable(true);
  connect(showNamesAction, &QAction::toggled, m_scene, &GeometryScene::setShowBlockNames);
  viewMenu->addAction("Show &Orphans", this, &MainWindow::onShowOrphansTriggered);
  viewMenu->addSeparator();
  QAction* statusBarAction = viewMenu->addAction("&Status Bar");
  statusBarAction->setCheckable(true);
  statusBarAction->setChecked(true);
  connect(statusBarAction, &QAction::toggled, statusBar(), &QStatusBar::setVisible);

  QMenu* gridMenu = menuBar()->addMenu("&Grid");
  QAction* showGridAction = gridMenu->addAction("&Show Grid");
  showGridAction->setCheckable(true);
  showGridAction->setChecked(m_scene->showGrid());
  connect(showGridAction, &QAction::toggled, m_scene, &GeometryScene::setShowGrid);
  QAction* snapGridAction = gridMenu->addAction("S&nap to Grid");
  snapGridAction->setCheckable(true);
  snapGridAction->setChecked(m_scene->snapToGrid());
  connect(snapGridAction, &QAction::toggled, m_scene, &GeometryScene::setSnapToGrid);
  gridMenu->addAction("S&et Grid...", this, &MainWindow::onSetGridTriggered);

  QMenu* helpMenu = menuBar()->addMenu("&Help");
  helpMenu->addAction("&Help Topics", this, &MainWindow::onHelpTopicsTriggered);
  helpMenu->addSeparator();
  helpMenu->addAction("&License", this, &MainWindow::onLicenseTriggered);
  helpMenu->addAction("&About FEMMX...", this, &MainWindow::onAboutTriggered);

  updateRecentFilesMenu();

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
  addToRecentFiles(femPath);
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
  addToRecentFiles(path);
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

// Zoom/Pan -- mirrors FemmeView.cpp's OnZoomIn/OnZoomOut/OnPan* (2x-per-
// step zoom, quarter-viewport-per-step pan), just expressed as relative
// QGraphicsView transforms/scrollbar deltas instead of raw GDI ox/oy/mag
// bookkeeping -- Qt's view already owns an equivalent transform, so there's
// no reason to duplicate that state on this side.
void MainWindow::onZoomIn()
{
  m_view->scale(2.0, 2.0);
}

void MainWindow::onZoomOut()
{
  m_view->scale(0.5, 0.5);
}

void MainWindow::onZoomNatural()
{
  QRectF bounds = m_scene->itemsBoundingRect();
  if (bounds.isEmpty())
    return;
  m_view->fitInView(bounds, Qt::KeepAspectRatio);
}

void MainWindow::onZoomWindowTriggered()
{
  m_scene->setToolMode(GeometryToolMode::ZoomWindow);
}

void MainWindow::onZoomWindowSelected(QRectF sceneRect)
{
  m_view->fitInView(sceneRect, Qt::KeepAspectRatio);
  // Match the toolbar's radio buttons back to Select, which is what the
  // scene itself reverted to internally (see GeometryScene::
  // mouseReleaseEvent) -- Zoom Window isn't part of that QActionGroup (see
  // its header comment), so nothing else re-checks this automatically.
  m_selectToolAction->setChecked(true);
}

void MainWindow::onPanLeft()
{
  auto* bar = m_view->horizontalScrollBar();
  bar->setValue(bar->value() - m_view->viewport()->width() / 4);
}

void MainWindow::onPanRight()
{
  auto* bar = m_view->horizontalScrollBar();
  bar->setValue(bar->value() + m_view->viewport()->width() / 4);
}

void MainWindow::onPanUp()
{
  auto* bar = m_view->verticalScrollBar();
  bar->setValue(bar->value() - m_view->viewport()->height() / 4);
}

void MainWindow::onPanDown()
{
  auto* bar = m_view->verticalScrollBar();
  bar->setValue(bar->value() + m_view->viewport()->height() / 4);
}

void MainWindow::onSetGridTriggered()
{
  bool ok = false;
  double size = QInputDialog::getDouble(this, "Set Grid", "Grid Spacing:", m_scene->gridSize(), 1e-6, 1e6, 6, &ok);
  if (ok)
    m_scene->setGridSize(size);
}

void MainWindow::snapshotForUndo()
{
  m_undoSnapshot = m_problem;
  m_hasUndo = true;
}

void MainWindow::onUndoTriggered()
{
  if (!m_hasUndo) {
    statusBar()->showMessage("Nothing to undo");
    return;
  }
  m_problem = m_undoSnapshot;
  m_hasUndo = false;
  m_scene->rebuild();
  markEdited();
  statusBar()->showMessage("Undone");
}

void MainWindow::onMoveSelectedTriggered()
{
  if (!m_scene->hasSelection()) {
    QMessageBox::information(this, "Move", "Nothing selected.");
    return;
  }
  bool ok = false;
  double dx = QInputDialog::getDouble(this, "Move", "Delta X:", 0.0, -1e9, 1e9, 6, &ok);
  if (!ok)
    return;
  double dy = QInputDialog::getDouble(this, "Move", "Delta Y:", 0.0, -1e9, 1e9, 6, &ok);
  if (!ok)
    return;

  snapshotForUndo();
  m_scene->syncSelectionToProblem();
  FemmProblemEdit::moveSelected(m_problem, dx, dy);
  m_scene->rebuild();
  markEdited();
}

void MainWindow::onCopySelectedTriggered()
{
  if (!m_scene->hasSelection()) {
    QMessageBox::information(this, "Copy", "Nothing selected.");
    return;
  }
  bool ok = false;
  double dx = QInputDialog::getDouble(this, "Copy", "Delta X:", 0.0, -1e9, 1e9, 6, &ok);
  if (!ok)
    return;
  double dy = QInputDialog::getDouble(this, "Copy", "Delta Y:", 0.0, -1e9, 1e9, 6, &ok);
  if (!ok)
    return;

  snapshotForUndo();
  m_scene->syncSelectionToProblem();
  FemmProblemEdit::copySelected(m_problem, dx, dy);
  m_scene->rebuild();
  markEdited();
}

void MainWindow::onScaleSelectedTriggered()
{
  if (!m_scene->hasSelection()) {
    QMessageBox::information(this, "Scale", "Nothing selected.");
    return;
  }
  bool ok = false;
  double factor = QInputDialog::getDouble(this, "Scale", "Scale Factor:", 1.0, 1e-9, 1e9, 6, &ok);
  if (!ok)
    return;
  double baseX = QInputDialog::getDouble(this, "Scale", "Base Point X:", 0.0, -1e9, 1e9, 6, &ok);
  if (!ok)
    return;
  double baseY = QInputDialog::getDouble(this, "Scale", "Base Point Y:", 0.0, -1e9, 1e9, 6, &ok);
  if (!ok)
    return;

  snapshotForUndo();
  m_scene->syncSelectionToProblem();
  FemmProblemEdit::scaleSelected(m_problem, baseX, baseY, factor);
  m_scene->rebuild();
  markEdited();
}

void MainWindow::onMirrorSelectedTriggered()
{
  if (!m_scene->hasSelection()) {
    QMessageBox::information(this, "Mirror", "Nothing selected.");
    return;
  }
  bool ok = false;
  double x0 = QInputDialog::getDouble(this, "Mirror", "Mirror Line Point 1, X:", 0.0, -1e9, 1e9, 6, &ok);
  if (!ok)
    return;
  double y0 = QInputDialog::getDouble(this, "Mirror", "Mirror Line Point 1, Y:", 0.0, -1e9, 1e9, 6, &ok);
  if (!ok)
    return;
  double x1 = QInputDialog::getDouble(this, "Mirror", "Mirror Line Point 2, X:", 0.0, -1e9, 1e9, 6, &ok);
  if (!ok)
    return;
  double y1 = QInputDialog::getDouble(this, "Mirror", "Mirror Line Point 2, Y:", 1.0, -1e9, 1e9, 6, &ok);
  if (!ok)
    return;

  snapshotForUndo();
  m_scene->syncSelectionToProblem();
  FemmProblemEdit::mirrorSelected(m_problem, x0, y0, x1, y1);
  m_scene->rebuild();
  markEdited();
}

void MainWindow::onCreateMeshTriggered()
{
  // Same "must be saved first" requirement as Solve -- MeshBuilder reads
  // geometry from the saved .fem, not m_problem directly (see
  // onSolveTriggered's comment).
  if (m_currentPath.isEmpty()) {
    onSaveAsTriggered();
    if (m_currentPath.isEmpty())
      return;
  } else if (!saveAs(m_currentPath)) {
    return;
  }

  if (hasAppliedPeriodicBoundary()) {
    QMessageBox::warning(this, "Cannot Mesh",
        "This problem uses a periodic or antiperiodic boundary condition, "
        "which this Qt GUI doesn't support meshing for yet. Open it in the "
        "classic FEMMX GUI instead.");
    return;
  }

  statusBar()->showMessage("Meshing...");
  QApplication::setOverrideCursor(Qt::WaitCursor);
  QString error;
  bool ok = SolveRunner::mesh(m_problem, m_currentPath, error);
  QApplication::restoreOverrideCursor();
  if (!ok) {
    statusBar()->showMessage("Meshing failed");
    QMessageBox::warning(this, "Mesh Failed", error);
    return;
  }

  QFileInfo fi(m_currentPath);
  QString rootPath = fi.absolutePath() + "/" + fi.completeBaseName();
  MeshOverlay mesh;
  if (!MeshOverlayIO::load(rootPath, mesh, error)) {
    statusBar()->showMessage("Meshing failed");
    QMessageBox::warning(this, "Mesh Failed", error);
    return;
  }

  m_scene->setMeshOverlay(mesh);
  m_scene->setShowMesh(true);
  m_showMeshAction->setChecked(true);
  statusBar()->showMessage(QString("Meshed -- %1 nodes, %2 elements").arg(mesh.nodes.size()).arg(mesh.elements.size()));
}

void MainWindow::onPurgeMeshTriggered()
{
  m_scene->clearMeshOverlay();
  m_showMeshAction->setChecked(false);

  // Matches femm.rc's "Purge Mesh" actually deleting the mesh files on
  // disk (not just hiding the overlay), so a subsequent Solve can't
  // accidentally pick up a stale mesh -- though SolveRunner::solve()
  // always regenerates them fresh anyway, this also mirrors the classic
  // GUI's own disk-cleanliness intent.
  if (!m_currentPath.isEmpty()) {
    QFileInfo fi(m_currentPath);
    QString rootPath = fi.absolutePath() + "/" + fi.completeBaseName();
    QFile::remove(rootPath + ".node");
    QFile::remove(rootPath + ".edge");
    QFile::remove(rootPath + ".ele");
  }
  statusBar()->showMessage("Mesh purged");
}

void MainWindow::onShowOrphansTriggered()
{
  if (m_scene->selectOrphans())
    statusBar()->showMessage("Orphaned nodes found and selected -- these likely mean an unclosed region.");
  else
    statusBar()->showMessage("No orphaned nodes found.");
}

void MainWindow::onSelectByGroupTriggered()
{
  bool ok = false;
  int group = QInputDialog::getInt(this, "Select by Group", "Group Number:", 0, 0, 1000000, 1, &ok);
  if (ok)
    m_scene->selectByGroup(group);
}

void MainWindow::onSetGroupTriggered()
{
  if (!m_scene->hasSelection()) {
    QMessageBox::information(this, "Set Group", "Nothing selected.");
    return;
  }
  bool ok = false;
  int group = QInputDialog::getInt(this, "Set Group", "Group Number:", 0, 0, 1000000, 1, &ok);
  if (!ok)
    return;
  m_scene->syncSelectionToProblem();
  m_scene->applyGroupToSelected(group);
  markEdited();
}

void MainWindow::addToRecentFiles(const QString& path)
{
  QSettings settings;
  QStringList recent = settings.value("recentFiles").toStringList();
  recent.removeAll(path);
  recent.prepend(path);
  while (recent.size() > 8)
    recent.removeLast();
  settings.setValue("recentFiles", recent);
  updateRecentFilesMenu();
}

void MainWindow::updateRecentFilesMenu()
{
  m_recentFilesMenu->clear();
  QSettings settings;
  QStringList recent = settings.value("recentFiles").toStringList();
  if (recent.isEmpty()) {
    QAction* empty = m_recentFilesMenu->addAction("(none)");
    empty->setEnabled(false);
    return;
  }
  for (const QString& path : recent) {
    QAction* action = m_recentFilesMenu->addAction(path);
    action->setData(path);
    connect(action, &QAction::triggered, this, &MainWindow::onOpenRecentFile);
  }
}

void MainWindow::onOpenRecentFile()
{
  auto* action = qobject_cast<QAction*>(sender());
  if (!action)
    return;
  QString path = action->data().toString();
  if (!QFileInfo::exists(path)) {
    QMessageBox::warning(this, "Open Failed", QStringLiteral("\"%1\" no longer exists.").arg(path));
    QSettings settings;
    QStringList recent = settings.value("recentFiles").toStringList();
    recent.removeAll(path);
    settings.setValue("recentFiles", recent);
    updateRecentFilesMenu();
    return;
  }
  if (!confirmDiscardUnsavedChanges())
    return;
  openFile(path);
}

void MainWindow::onHelpTopicsTriggered()
{
  // femm.rc's "Help Topics" opens a compiled .chm; this GUI has no
  // equivalent yet, so this opens the same LaTeX-built manual.pdf the
  // classic GUI's installer also ships (see manual/build_manual.bat) --
  // checked next to the exe first (where a real install would put it),
  // falling back to the source-tree location for a dev build.
  QString exeDir = QCoreApplication::applicationDirPath();
  QStringList candidates = {
    exeDir + "/manual.pdf",
    exeDir + "/../manual/manual.pdf",
    exeDir + "/../../manual/manual.pdf",
  };
  for (const QString& candidate : candidates) {
    if (QFileInfo::exists(candidate)) {
      QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(candidate).absoluteFilePath()));
      return;
    }
  }
  QMessageBox::information(this, "Help Topics",
      "manual.pdf wasn't found. Build it with manual/build_manual.bat, "
      "or see the FEMM documentation at https://www.femm.info/.");
}

void MainWindow::onLicenseTriggered()
{
  QString exeDir = QCoreApplication::applicationDirPath();
  QFile file(exeDir + "/license.txt");
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QMessageBox::information(this, "License", "license.txt wasn't found next to femmqt.exe.");
    return;
  }
  QString text = QString::fromUtf8(file.readAll());

  QDialog dlg(this);
  dlg.setWindowTitle("License");
  dlg.resize(600, 500);
  auto* layout = new QVBoxLayout(&dlg);
  auto* view = new QPlainTextEdit(text, &dlg);
  view->setReadOnly(true);
  layout->addWidget(view);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  layout->addWidget(buttons);
  dlg.exec();
}

void MainWindow::onAboutTriggered()
{
  QMessageBox::about(this, "About FEMMX",
      "<b>FEMMX (Qt)</b> -- Magnetics Editor<br><br>"
      "A Qt6-based GUI for FEMMX, alongside the classic Windows GUI "
      "(femmx.exe). Shares the same .fem/.ans file formats and solver "
      "executables (triangle.exe, fkn.exe).<br><br>"
      "See File &gt; Switch to Classic GUI to use the original interface.");
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
