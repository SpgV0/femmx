#include "MainWindow.h"

#include "FemmFileIO.h"
#include "FemxFileIO.h"
#include "GuiSwitch.h"
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
#include <QStatusBar>
#include <QToolBar>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
  resize(1024, 768);

  m_scene = new GeometryScene(this);
  m_scene->setProblem(&m_problem);
  connect(m_scene, &GeometryScene::problemEdited, this, &MainWindow::onProblemEdited);

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

  QToolBar* toolBar = addToolBar("Draw");
  auto* toolGroup = new QActionGroup(this);
  toolGroup->setExclusive(true);

  m_selectToolAction = toolBar->addAction("Select");
  m_selectToolAction->setCheckable(true);
  m_selectToolAction->setChecked(true);
  toolGroup->addAction(m_selectToolAction);
  connect(m_selectToolAction, &QAction::triggered, this, [this]() { m_scene->setToolMode(GeometryToolMode::Select); });

  m_addNodeToolAction = toolBar->addAction("Add Node");
  m_addNodeToolAction->setCheckable(true);
  toolGroup->addAction(m_addNodeToolAction);
  connect(m_addNodeToolAction, &QAction::triggered, this, [this]() { m_scene->setToolMode(GeometryToolMode::AddNode); });

  m_addSegmentToolAction = toolBar->addAction("Add Segment");
  m_addSegmentToolAction->setCheckable(true);
  toolGroup->addAction(m_addSegmentToolAction);
  connect(m_addSegmentToolAction, &QAction::triggered, this, [this]() { m_scene->setToolMode(GeometryToolMode::AddSegment); });

  m_addBlockLabelToolAction = toolBar->addAction("Add Block Label");
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
  m_dirty = true;
  updateTitle();
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
