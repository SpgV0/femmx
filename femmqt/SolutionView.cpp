#include "SolutionView.h"

#include "AnsFileIO.h"
#include "AppPreferences.h"
#include "AppTheme.h"
#include "AnsxFileIO.h"
#include "FemmFileIO.h"
#include "FemmProblem.h"
#include "GuiSwitch.h"
#include "MainWindow.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>

namespace {
constexpr int kNumBands = 20;

// Simple fixed "cold to hot" colormap (blue -> cyan -> green -> yellow ->
// red), band 0 = lowest |B|, band 19 = highest -- a fresh, Qt-side
// implementation of the same color-banding concept as
// femm/FemmviewView.cpp's PlotFluxDensity, not shared code (matches this
// phase's scope: no Preferences-configurable legend colors yet).
QColor bandColor(int band)
{
  double t = band / (double)(kNumBands - 1); // 0..1
  return QColor::fromHsvF((1.0 - t) * 0.667, 1.0, 1.0); // hue 240deg(blue) -> 0deg(red)
}

// Point-in-triangle via barycentric sign test (standard technique).
bool pointInTriangle(QPointF p, QPointF a, QPointF b, QPointF c, double& u, double& v, double& w)
{
  double denom = (b.y() - c.y()) * (a.x() - c.x()) + (c.x() - b.x()) * (a.y() - c.y());
  if (std::abs(denom) < 1e-300)
    return false;
  u = ((b.y() - c.y()) * (p.x() - c.x()) + (c.x() - b.x()) * (p.y() - c.y())) / denom;
  v = ((c.y() - a.y()) * (p.x() - c.x()) + (a.x() - c.x()) * (p.y() - c.y())) / denom;
  w = 1.0 - u - v;
  const double eps = -1e-9;
  return u >= eps && v >= eps && w >= eps;
}

double triangleArea(QPointF a, QPointF b, QPointF c)
{
  return 0.5 * std::abs((b.x() - a.x()) * (c.y() - a.y()) - (c.x() - a.x()) * (b.y() - a.y()));
}
} // namespace

MeshSolutionItem::MeshSolutionItem(const MeshSolution* solution)
    : m_solution(solution)
{
  double xmin = 0, xmax = 0, ymin = 0, ymax = 0;
  bool first = true;
  for (const MeshSolutionNode& n : solution->nodes) {
    if (first) {
      xmin = xmax = n.x;
      ymin = ymax = n.y;
      first = false;
    } else {
      xmin = std::min(xmin, n.x);
      xmax = std::max(xmax, n.x);
      ymin = std::min(ymin, n.y);
      ymax = std::max(ymax, n.y);
    }
  }
  m_bounds = QRectF(QPointF(xmin, ymin), QPointF(xmax, ymax));

  // Precompute each node's average |B| across every element touching it
  // -- see this member's header comment for why (approximates "Smooth"
  // shading). Done once here rather than per-paint, since paint() can run
  // many times (every pan/zoom) but the mesh itself never changes.
  m_nodeBMagAvg.fill(0.0, solution->nodes.size());
  QVector<int> touchCount(solution->nodes.size(), 0);
  for (const MeshSolutionElement& e : solution->elements) {
    double bMag = std::hypot(std::hypot(e.B1re, e.B1im), std::hypot(e.B2re, e.B2im));
    for (int p : { e.p0, e.p1, e.p2 }) {
      if (p >= 0 && p < m_nodeBMagAvg.size()) {
        m_nodeBMagAvg[p] += bMag;
        touchCount[p]++;
      }
    }
  }
  for (int i = 0; i < m_nodeBMagAvg.size(); i++)
    if (touchCount[i] > 0)
      m_nodeBMagAvg[i] /= touchCount[i];
}

QRectF MeshSolutionItem::boundingRect() const
{
  return m_bounds;
}

void MeshSolutionItem::setPlotMode(PlotMode mode)
{
  m_mode = mode;
  update();
}

void MeshSolutionItem::setSmoothing(bool smooth)
{
  m_smooth = smooth;
  update();
}

void MeshSolutionItem::setShowMesh(bool show)
{
  m_showMesh = show;
  update();
}

void MeshSolutionItem::setShowPoints(bool show)
{
  m_showPoints = show;
  update();
}

void MeshSolutionItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
  if (!m_solution || m_solution->elements.isEmpty())
    return;
  switch (m_mode) {
  case PlotMode::Density: paintDensity(painter); break;
  case PlotMode::Contour: paintContour(painter); break;
  case PlotMode::Vector: paintVector(painter); break;
  }
  if (m_showMesh || m_showPoints)
    paintMeshOverlay(painter);
}

void MeshSolutionItem::paintDensity(QPainter* painter)
{
  double bMin = m_solution->bMagMin, bMax = m_solution->bMagMax;
  double span = bMax - bMin;

  // One QPainterPath per color band, filled with a single fillPath() call
  // each -- O(kNumBands) draw calls regardless of element count, the same
  // batching principle as PolyPolygon()/FlushDensityBand in the MFC GUI.
  QPainterPath bandPaths[kNumBands];

  for (const MeshSolutionElement& e : m_solution->elements) {
    if (e.p0 < 0 || e.p0 >= m_solution->nodes.size() || e.p1 < 0 || e.p1 >= m_solution->nodes.size() || e.p2 < 0 || e.p2 >= m_solution->nodes.size())
      continue;
    // Smooth: band on the average of the 3 corners' node-averaged |B|
    // instead of this element's own single value -- see m_nodeBMagAvg's
    // header comment.
    double bMag = m_smooth
        ? (m_nodeBMagAvg[e.p0] + m_nodeBMagAvg[e.p1] + m_nodeBMagAvg[e.p2]) / 3.0
        : std::hypot(std::hypot(e.B1re, e.B1im), std::hypot(e.B2re, e.B2im));
    int band = (span > 0) ? (int)((bMag - bMin) / span * kNumBands) : 0;
    if (band >= kNumBands)
      band = kNumBands - 1;
    if (band < 0)
      band = 0;

    const MeshSolutionNode& n0 = m_solution->nodes[e.p0];
    const MeshSolutionNode& n1 = m_solution->nodes[e.p1];
    const MeshSolutionNode& n2 = m_solution->nodes[e.p2];

    QPolygonF tri;
    tri << QPointF(n0.x, n0.y) << QPointF(n1.x, n1.y) << QPointF(n2.x, n2.y);
    bandPaths[band].addPolygon(tri);
  }

  painter->setPen(Qt::NoPen);
  for (int b = 0; b < kNumBands; b++) {
    if (bandPaths[b].isEmpty())
      continue;
    painter->setBrush(bandColor(b));
    painter->drawPath(bandPaths[b]);
  }
}

void MeshSolutionItem::paintContour(QPainter* painter)
{
  // Equipotential lines of Re(A) (the DC/instantaneous-snapshot potential
  // -- see this function's header note in SolutionView.h) via per-triangle
  // marching-triangles: for each of a fixed number of evenly-spaced
  // levels between the mesh's Are extremes, find where that level crosses
  // each triangle's edges (linear interpolation along the edge) and draw
  // the resulting segment. Not the classic GUI's exact contour algorithm,
  // but the same visual result for a piecewise-linear field.
  if (m_solution->nodes.isEmpty())
    return;
  double aMin = m_solution->nodes[0].Are, aMax = aMin;
  for (const MeshSolutionNode& n : m_solution->nodes) {
    aMin = std::min(aMin, n.Are);
    aMax = std::max(aMax, n.Are);
  }
  double span = aMax - aMin;
  if (span <= 0)
    return;

  constexpr int kNumLevels = 20;
  QPainterPath path;
  for (const MeshSolutionElement& e : m_solution->elements) {
    if (e.p0 < 0 || e.p0 >= m_solution->nodes.size() || e.p1 < 0 || e.p1 >= m_solution->nodes.size() || e.p2 < 0 || e.p2 >= m_solution->nodes.size())
      continue;
    const MeshSolutionNode& n0 = m_solution->nodes[e.p0];
    const MeshSolutionNode& n1 = m_solution->nodes[e.p1];
    const MeshSolutionNode& n2 = m_solution->nodes[e.p2];
    double va[3] = { n0.Are, n1.Are, n2.Are };
    QPointF pa[3] = { QPointF(n0.x, n0.y), QPointF(n1.x, n1.y), QPointF(n2.x, n2.y) };

    for (int lvl = 1; lvl < kNumLevels; lvl++) {
      double level = aMin + span * lvl / kNumLevels;
      QPointF crossings[2];
      int found = 0;
      for (int edge = 0; edge < 3 && found < 2; edge++) {
        int i0 = edge, i1 = (edge + 1) % 3;
        double v0 = va[i0], v1 = va[i1];
        if ((v0 <= level && v1 > level) || (v1 <= level && v0 > level)) {
          double t = (level - v0) / (v1 - v0);
          crossings[found++] = pa[i0] + t * (pa[i1] - pa[i0]);
        }
      }
      if (found == 2) {
        path.moveTo(crossings[0]);
        path.lineTo(crossings[1]);
      }
    }
  }

  QPen pen(AppTheme::meshPointColor());
  pen.setCosmetic(true);
  painter->setPen(pen);
  painter->drawPath(path);
}

void MeshSolutionItem::paintVector(QPainter* painter)
{
  // Fixed-length arrows at each element centroid showing Re(B) direction
  // (see paintContour's Re(.) note) -- a qualitative field-direction plot,
  // not scaled to physical magnitude (|B| already varies over orders of
  // magnitude across a typical mesh, which would make a magnitude-scaled
  // arrow plot mostly invisible/unreadably-huge in the same view).
  double diag = std::hypot(m_bounds.width(), m_bounds.height());
  double arrowLen = diag * 0.01;
  if (arrowLen <= 0)
    return;

  QPen pen(AppTheme::meshPointColor());
  pen.setCosmetic(true);
  painter->setPen(pen);

  // Sampling every element would be too dense to read -- skip through at
  // a stride so roughly a few thousand arrows are drawn regardless of
  // mesh size.
  int stride = std::max(1, (int)(m_solution->elements.size() / 3000));
  for (int i = 0; i < m_solution->elements.size(); i += stride) {
    const MeshSolutionElement& e = m_solution->elements[i];
    double bx = e.B1re, by = e.B2re;
    double mag = std::hypot(bx, by);
    if (mag <= 0)
      continue;
    double ux = bx / mag, uy = by / mag;
    QPointF center(e.ctrX, e.ctrY);
    QPointF tip = center + QPointF(ux, uy) * arrowLen;
    painter->drawLine(center, tip);
    QPointF perp(-uy, ux);
    QPointF back = tip - QPointF(ux, uy) * (arrowLen * 0.3);
    painter->drawLine(tip, back + perp * (arrowLen * 0.15));
    painter->drawLine(tip, back - perp * (arrowLen * 0.15));
  }
}

void MeshSolutionItem::paintMeshOverlay(QPainter* painter)
{
  // Drawn on top of whichever plot mode is active (femm.rc's Show Mesh/
  // Show Points are independent toggles, not plot modes of their own).
  if (m_showMesh) {
    QPainterPath path;
    for (const MeshSolutionElement& e : m_solution->elements) {
      if (e.p0 < 0 || e.p0 >= m_solution->nodes.size() || e.p1 < 0 || e.p1 >= m_solution->nodes.size() || e.p2 < 0 || e.p2 >= m_solution->nodes.size())
        continue;
      const MeshSolutionNode& n0 = m_solution->nodes[e.p0];
      const MeshSolutionNode& n1 = m_solution->nodes[e.p1];
      const MeshSolutionNode& n2 = m_solution->nodes[e.p2];
      path.moveTo(n0.x, n0.y);
      path.lineTo(n1.x, n1.y);
      path.lineTo(n2.x, n2.y);
      path.lineTo(n0.x, n0.y);
    }
    QPen pen(AppTheme::meshLineColor());
    pen.setCosmetic(true);
    painter->setPen(pen);
    painter->drawPath(path);
  }

  if (m_showPoints) {
    double diag = std::hypot(m_bounds.width(), m_bounds.height());
    double r = diag * 0.0015;
    painter->setPen(Qt::NoPen);
    painter->setBrush(AppTheme::meshPointColor());
    for (const MeshSolutionNode& n : m_solution->nodes)
      painter->drawEllipse(QPointF(n.x, n.y), r, r);
  }
}

SolutionGraphicsView::SolutionGraphicsView(QGraphicsScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
{
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  setResizeAnchor(QGraphicsView::AnchorUnderMouse);
}

void SolutionGraphicsView::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton)
    emit clickedAt(mapToScene(event->pos()));
  QGraphicsView::mousePressEvent(event);
}

void SolutionGraphicsView::wheelEvent(QWheelEvent* event)
{
  double factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
  scale(factor, factor);
  updateAntialiasingForScale();
  event->accept();
}

void SolutionGraphicsView::updateAntialiasingForScale()
{
  // m11() is the transform's horizontal scale factor -- the y-flip
  // (scale(1,-1) applied once at construction) makes m22() negative, so
  // m11() alone is the right one to threshold on. 8x was picked
  // empirically: comfortably past the zoom level where the seam artifact
  // became visible in testing, comfortably before it costs anything on a
  // multi-million-element mesh (at 8x+, the visible viewport can no
  // longer contain more than a small fraction of a typical mesh).
  bool zoomedInEnough = std::abs(transform().m11()) >= 8.0;
  setRenderHint(QPainter::Antialiasing, zoomedInEnough);
}

SolutionWindow::SolutionWindow(QWidget* parent)
    : QMainWindow(parent)
{
  setWindowTitle("FEMMX (Qt) - Solution Viewer");
  resize(1024, 768);

  m_scene = new QGraphicsScene(this);
  m_scene->setBackgroundBrush(AppTheme::background());
  m_view = new SolutionGraphicsView(m_scene, this);
  m_view->setRenderHint(QPainter::Antialiasing, false); // large meshes: skip AA for speed
  // Matches MainWindow's view->scale(1,-1): .ans geometry is in the same
  // math (y-up) convention as .fem.
  m_view->scale(1, -1);
  setCentralWidget(m_view);
  connect(m_view, &SolutionGraphicsView::clickedAt, this, &SolutionWindow::onCanvasClicked);

  m_outputDock = new QDockWidget("Output Window", this);
  m_outputText = new QPlainTextEdit(m_outputDock);
  m_outputText->setReadOnly(true);
  m_outputText->setMaximumBlockCount(2000);
  m_outputDock->setWidget(m_outputText);
  addDockWidget(Qt::BottomDockWidgetArea, m_outputDock);
  m_outputDock->setVisible(AppPreferences::load().showOutputWindow);

  QMenu* fileMenu = menuBar()->addMenu("&File");
  fileMenu->addAction("&Open Solution...", this, &SolutionWindow::onOpenTriggered, QKeySequence::Open);
  fileMenu->addAction("&Reload", this, &SolutionWindow::onReloadTriggered);
  fileMenu->addSeparator();
  m_recentFilesMenu = fileMenu->addMenu("Recent Files");
  fileMenu->addSeparator();
  fileMenu->addAction("Switch to &Classic GUI...", this, &SolutionWindow::onSwitchToClassicTriggered);
  fileMenu->addSeparator();
  fileMenu->addAction("E&xit", this, &QWidget::close);

  QMenu* editMenu = menuBar()->addMenu("&Edit");
  editMenu->addAction("Copy as &Bitmap", this, &SolutionWindow::onCopyBitmapTriggered);

  QMenu* zoomMenu = menuBar()->addMenu("&Zoom");
  zoomMenu->addAction("Zoom &In", this, &SolutionWindow::onZoomIn, QKeySequence(Qt::Key_PageUp));
  zoomMenu->addAction("Zoom &Out", this, &SolutionWindow::onZoomOut, QKeySequence(Qt::Key_PageDown));
  zoomMenu->addAction("&Natural", this, &SolutionWindow::onZoomNatural, QKeySequence(Qt::Key_Home));
  zoomMenu->addSeparator();
  zoomMenu->addAction("Scroll &Left", this, &SolutionWindow::onPanLeft, QKeySequence(Qt::Key_Left));
  zoomMenu->addAction("Scroll &Right", this, &SolutionWindow::onPanRight, QKeySequence(Qt::Key_Right));
  zoomMenu->addAction("Scroll &Up", this, &SolutionWindow::onPanUp, QKeySequence(Qt::Key_Up));
  zoomMenu->addAction("Scroll &Down", this, &SolutionWindow::onPanDown, QKeySequence(Qt::Key_Down));

  QMenu* viewMenu = menuBar()->addMenu("&View");
  auto* plotGroup = new QActionGroup(this);
  plotGroup->setExclusive(true);
  QAction* densityAction = viewMenu->addAction("&Density Plot");
  densityAction->setCheckable(true);
  densityAction->setChecked(true);
  plotGroup->addAction(densityAction);
  connect(densityAction, &QAction::triggered, this, [this]() { if (m_item) m_item->setPlotMode(MeshSolutionItem::PlotMode::Density); });
  QAction* contourAction = viewMenu->addAction("&Contour Plot");
  contourAction->setCheckable(true);
  plotGroup->addAction(contourAction);
  connect(contourAction, &QAction::triggered, this, [this]() { if (m_item) m_item->setPlotMode(MeshSolutionItem::PlotMode::Contour); });
  QAction* vectorAction = viewMenu->addAction("&Vector Plot");
  vectorAction->setCheckable(true);
  plotGroup->addAction(vectorAction);
  connect(vectorAction, &QAction::triggered, this, [this]() { if (m_item) m_item->setPlotMode(MeshSolutionItem::PlotMode::Vector); });
  QAction* smoothAction = viewMenu->addAction("&Smoothing");
  smoothAction->setCheckable(true);
  smoothAction->setChecked(true);
  connect(smoothAction, &QAction::toggled, this, [this](bool on) { if (m_item) m_item->setSmoothing(on); });
  viewMenu->addSeparator();
  QAction* showMeshAction = viewMenu->addAction("Show &Mesh");
  showMeshAction->setCheckable(true);
  connect(showMeshAction, &QAction::toggled, this, [this](bool on) { if (m_item) m_item->setShowMesh(on); });
  QAction* showPointsAction = viewMenu->addAction("Show &Points");
  showPointsAction->setCheckable(true);
  connect(showPointsAction, &QAction::toggled, this, [this](bool on) { if (m_item) m_item->setShowPoints(on); });
  viewMenu->addSeparator();
  viewMenu->addAction("Problem &Info...", this, &SolutionWindow::onProblemInfoTriggered);
  viewMenu->addSeparator();
  QAction* outputWindowAction = viewMenu->addAction("&Output Window");
  outputWindowAction->setCheckable(true);
  outputWindowAction->setChecked(AppPreferences::load().showOutputWindow);
  connect(outputWindowAction, &QAction::toggled, m_outputDock, &QDockWidget::setVisible);
  viewMenu->addSeparator();
  QAction* statusBarAction = viewMenu->addAction("&Status Bar");
  statusBarAction->setCheckable(true);
  statusBarAction->setChecked(true);
  connect(statusBarAction, &QAction::toggled, statusBar(), &QStatusBar::setVisible);
  viewMenu->addSeparator();
  QAction* darkThemeAction = viewMenu->addAction("&Dark Theme");
  darkThemeAction->setCheckable(true);
  darkThemeAction->setChecked(AppTheme::isDark());
  connect(darkThemeAction, &QAction::toggled, this, [this](bool dark) {
    AppTheme::setDark(dark);
    AppPreferences prefs = AppPreferences::load();
    prefs.darkTheme = dark;
    prefs.save();
    m_scene->setBackgroundBrush(AppTheme::background());
    m_scene->update();
  });

  // Matches femm.rc's post-processor "Operation" menu (Point properties /
  // Contours / Areas). Plot X-Y and Integrate are both separate top-level
  // commands there too (not nested in Operation) -- matched here the same
  // way, both operating on "the contour currently drawn."
  QMenu* opMenu = menuBar()->addMenu("&Operation");
  m_pointToolAction = opMenu->addAction("&Point Properties");
  m_pointToolAction->setCheckable(true);
  m_contourToolAction = opMenu->addAction("&Contours");
  m_contourToolAction->setCheckable(true);
  m_areaToolAction = opMenu->addAction("&Areas");
  m_areaToolAction->setCheckable(true);
  auto* toolGroup = new QActionGroup(this);
  toolGroup->setExclusive(true);
  toolGroup->addAction(m_pointToolAction);
  toolGroup->addAction(m_contourToolAction);
  toolGroup->addAction(m_areaToolAction);
  connect(m_pointToolAction, &QAction::triggered, this, &SolutionWindow::onPointToolTriggered);
  connect(m_contourToolAction, &QAction::triggered, this, &SolutionWindow::onContourToolTriggered);
  connect(m_areaToolAction, &QAction::triggered, this, &SolutionWindow::onAreaToolTriggered);
  opMenu->addSeparator();
  opMenu->addAction("&Finish Contour", this, &SolutionWindow::onFinishContourTriggered);
  opMenu->addAction("&Clear Contour", this, &SolutionWindow::onClearContourTriggered);
  menuBar()->addAction("Plot &X-Y", this, &SolutionWindow::onPlotXYTriggered);
  menuBar()->addAction("&Integrate", this, &SolutionWindow::onIntegrateTriggered);

  QMenu* helpMenu = menuBar()->addMenu("&Help");
  helpMenu->addAction("&Help Topics", this, &SolutionWindow::onHelpTopicsTriggered);
  helpMenu->addSeparator();
  helpMenu->addAction("&License", this, &SolutionWindow::onLicenseTriggered);
  helpMenu->addAction("&About FEMMX...", this, &SolutionWindow::onAboutTriggered);

  updateRecentFilesMenu();

  statusBar()->showMessage("Ready");
}

void SolutionWindow::onOpenTriggered()
{
  QString path = QFileDialog::getOpenFileName(this, "Open Solved Magnetics Problem", QString(),
      "FEMM Solution Files (*.ans *.ansx)");
  if (path.isEmpty())
    return;
  openAnsFile(path);
}

void SolutionWindow::openAnsFile(const QString& path)
{
  QFileInfo pathInfo(path);
  QString ansPath = path;
  QString ansxPath = pathInfo.absolutePath() + "/" + pathInfo.completeBaseName() + ".ansx";
  if (pathInfo.suffix().compare("ansx", Qt::CaseInsensitive) == 0) {
    // Opened directly by extension -- still need a sibling .ans to check
    // staleness against and to fall back to if the .ansx is corrupt/stale
    // with no .ans around to regenerate from.
    ansPath = pathInfo.absolutePath() + "/" + pathInfo.completeBaseName() + ".ans";
    ansxPath = path;
  }

  QElapsedTimer timer;
  timer.start();
  QString error;
  bool loadedFromAnsx = false;

  if (AnsxFileIO::isUpToDate(ansxPath, ansPath)) {
    if (AnsxFileIO::readAnsx(ansxPath, m_solution, error))
      loadedFromAnsx = true;
    // falls through to the slow .ans path below if the .ansx turned out
    // to be corrupt despite passing the staleness check
  }

  if (!loadedFromAnsx) {
    FemmProblem problem;
    if (!QFileInfo::exists(ansPath)) {
      QMessageBox::warning(this, "Open Failed",
          QStringLiteral("\"%1\" doesn't exist and no matching .ansx cache was found.").arg(ansPath));
      return;
    }
    if (!AnsFileIO::readAns(ansPath, problem, m_solution, error)) {
      QMessageBox::warning(this, "Open Failed", error);
      return;
    }
    // Cache for next time -- best-effort: a failure here (e.g. a
    // read-only directory) shouldn't block viewing the solution we
    // already have loaded, just means no speedup next time.
    QString writeError;
    AnsxFileIO::writeAnsx(ansxPath, ansPath, (int)problem.problemType, (int)problem.lengthUnits,
        problem.frequency, m_solution, writeError);
  }

  qint64 elapsedMs = timer.elapsed();

  m_scene->clear();
  m_contourVisual = nullptr; // clear() above already deleted it
  m_contourPoints.clear();
  m_item = new MeshSolutionItem(&m_solution);
  m_scene->addItem(m_item);
  m_view->fitInView(m_item->boundingRect(), Qt::KeepAspectRatio);
  m_view->updateAntialiasingForScale();
  m_currentPath = ansPath;

  statusBar()->showMessage(QString("%1 -- %2 mesh nodes, %3 elements, |B| %4 to %5 T (loaded via %6 in %7 ms)")
                                .arg(path)
                                .arg(m_solution.nodes.size())
                                .arg(m_solution.elements.size())
                                .arg(m_solution.bMagMin, 0, 'g', 4)
                                .arg(m_solution.bMagMax, 0, 'g', 4)
                                .arg(loadedFromAnsx ? ".ansx" : ".ans")
                                .arg(elapsedMs));
  setWindowTitle(QString("FEMMX (Qt) - Solution Viewer - %1").arg(path));
  addToRecentFiles(path);
}

int SolutionWindow::findContainingElement(QPointF pt) const
{
  // Linear scan: acceptable here because this only runs once per
  // deliberate user click (Point/Area tools), not per frame -- a spatial
  // index (grid/quadtree bucketing elements by centroid) would be the
  // right fix if this ever needs to run in a loop, but a single click's
  // worth of latency on even a multi-million-element mesh is milliseconds.
  for (int i = 0; i < m_solution.elements.size(); i++) {
    const MeshSolutionElement& e = m_solution.elements[i];
    if (e.p0 < 0 || e.p0 >= m_solution.nodes.size() || e.p1 < 0 || e.p1 >= m_solution.nodes.size() || e.p2 < 0 || e.p2 >= m_solution.nodes.size())
      continue;
    const MeshSolutionNode& n0 = m_solution.nodes[e.p0];
    const MeshSolutionNode& n1 = m_solution.nodes[e.p1];
    const MeshSolutionNode& n2 = m_solution.nodes[e.p2];
    double u, v, w;
    if (pointInTriangle(pt, QPointF(n0.x, n0.y), QPointF(n1.x, n1.y), QPointF(n2.x, n2.y), u, v, w))
      return i;
  }
  return -1;
}

std::complex<double> SolutionWindow::interpolateA(QPointF pt, int elementIndex) const
{
  const MeshSolutionElement& e = m_solution.elements[elementIndex];
  const MeshSolutionNode& n0 = m_solution.nodes[e.p0];
  const MeshSolutionNode& n1 = m_solution.nodes[e.p1];
  const MeshSolutionNode& n2 = m_solution.nodes[e.p2];
  double u, v, w;
  pointInTriangle(pt, QPointF(n0.x, n0.y), QPointF(n1.x, n1.y), QPointF(n2.x, n2.y), u, v, w);
  double re = u * n0.Are + v * n1.Are + w * n2.Are;
  double im = u * n0.Aim + v * n1.Aim + w * n2.Aim;
  return { re, im };
}

void SolutionWindow::onPointToolTriggered()
{
  m_toolMode = SolutionToolMode::Point;
  statusBar()->showMessage("Point Properties: click a point on the mesh.");
}

void SolutionWindow::onContourToolTriggered()
{
  m_toolMode = SolutionToolMode::Contour;
  statusBar()->showMessage("Contours: click points to build a contour, then Operation > Finish Contour.");
}

void SolutionWindow::onAreaToolTriggered()
{
  m_toolMode = SolutionToolMode::Area;
  statusBar()->showMessage("Areas: click inside a region.");
}

void SolutionWindow::onCanvasClicked(QPointF scenePos)
{
  if (!m_item)
    return;

  switch (m_toolMode) {
  case SolutionToolMode::None:
    return;
  case SolutionToolMode::Point: {
    int elem = findContainingElement(scenePos);
    if (elem < 0) {
      statusBar()->showMessage("No mesh element at that point.");
      return;
    }
    std::complex<double> A = interpolateA(scenePos, elem);
    const MeshSolutionElement& e = m_solution.elements[elem];
    double bMag = std::hypot(std::hypot(e.B1re, e.B1im), std::hypot(e.B2re, e.B2im));

    QDialog dlg(this);
    dlg.setWindowTitle("Point Properties");
    auto* form = new QFormLayout(&dlg);
    form->addRow("x, y:", new QLabel(QString("%1, %2").arg(scenePos.x(), 0, 'g', 6).arg(scenePos.y(), 0, 'g', 6)));
    form->addRow("A (re, im):", new QLabel(QString("%1, %2").arg(A.real(), 0, 'g', 6).arg(A.imag(), 0, 'g', 6)));
    form->addRow("B1 (re, im):", new QLabel(QString("%1, %2").arg(e.B1re, 0, 'g', 6).arg(e.B1im, 0, 'g', 6)));
    form->addRow("B2 (re, im):", new QLabel(QString("%1, %2").arg(e.B2re, 0, 'g', 6).arg(e.B2im, 0, 'g', 6)));
    form->addRow("|B|:", new QLabel(QString("%1 T").arg(bMag, 0, 'g', 6)));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    form->addRow(buttons);
    appendOutput(QString("Point: x=%1, y=%2  A=(%3, %4)  B1=(%5, %6)  B2=(%7, %8)  |B|=%9 T")
                      .arg(scenePos.x(), 0, 'g', 6)
                      .arg(scenePos.y(), 0, 'g', 6)
                      .arg(A.real(), 0, 'g', 6)
                      .arg(A.imag(), 0, 'g', 6)
                      .arg(e.B1re, 0, 'g', 6)
                      .arg(e.B1im, 0, 'g', 6)
                      .arg(e.B2re, 0, 'g', 6)
                      .arg(e.B2im, 0, 'g', 6)
                      .arg(bMag, 0, 'g', 6));
    dlg.exec();
    break;
  }
  case SolutionToolMode::Contour:
    m_contourPoints.push_back(scenePos);
    updateContourVisual();
    statusBar()->showMessage(QString("Contour: %1 point(s). Operation > Finish Contour when done.").arg(m_contourPoints.size()));
    break;
  case SolutionToolMode::Area: {
    int elem = findContainingElement(scenePos);
    if (elem < 0) {
      statusBar()->showMessage("No mesh element at that point.");
      return;
    }
    int lbl = m_solution.elements[elem].lbl;
    double totalArea = 0;
    double bSum = 0;
    int count = 0;
    for (const MeshSolutionElement& e : m_solution.elements) {
      if (e.lbl != lbl)
        continue;
      if (e.p0 < 0 || e.p0 >= m_solution.nodes.size() || e.p1 < 0 || e.p1 >= m_solution.nodes.size() || e.p2 < 0 || e.p2 >= m_solution.nodes.size())
        continue;
      const MeshSolutionNode& n0 = m_solution.nodes[e.p0];
      const MeshSolutionNode& n1 = m_solution.nodes[e.p1];
      const MeshSolutionNode& n2 = m_solution.nodes[e.p2];
      double area = triangleArea(QPointF(n0.x, n0.y), QPointF(n1.x, n1.y), QPointF(n2.x, n2.y));
      totalArea += area;
      bSum += area * std::hypot(std::hypot(e.B1re, e.B1im), std::hypot(e.B2re, e.B2im));
      count++;
    }
    double avgB = totalArea > 0 ? bSum / totalArea : 0;

    QDialog dlg(this);
    dlg.setWindowTitle("Area Properties");
    auto* form = new QFormLayout(&dlg);
    form->addRow("Block label index:", new QLabel(QString::number(lbl)));
    form->addRow("Elements:", new QLabel(QString::number(count)));
    form->addRow("Area:", new QLabel(QString("%1").arg(totalArea, 0, 'g', 6)));
    form->addRow("Area-weighted avg |B|:", new QLabel(QString("%1 T").arg(avgB, 0, 'g', 6)));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    form->addRow(buttons);
    appendOutput(QString("Area: block label %1  elements=%2  area=%3  avg|B|=%4 T")
                      .arg(lbl)
                      .arg(count)
                      .arg(totalArea, 0, 'g', 6)
                      .arg(avgB, 0, 'g', 6));
    dlg.exec();
    break;
  }
  }
}

void SolutionWindow::updateContourVisual()
{
  if (m_contourVisual) {
    m_scene->removeItem(m_contourVisual);
    delete m_contourVisual;
    m_contourVisual = nullptr;
  }
  if (m_contourPoints.size() < 2)
    return;
  QPainterPath path;
  path.moveTo(m_contourPoints[0]);
  for (int i = 1; i < m_contourPoints.size(); i++)
    path.lineTo(m_contourPoints[i]);
  QPen pen(Qt::magenta);
  pen.setCosmetic(true);
  pen.setWidth(2);
  auto* item = m_scene->addPath(path, pen);
  item->setZValue(10.0);
  m_contourVisual = item;
}

void SolutionWindow::onFinishContourTriggered()
{
  showContourIntegral();
}

void SolutionWindow::showContourIntegral()
{
  if (m_contourPoints.size() < 2) {
    QMessageBox::information(this, "Contour Properties", "Click at least two points first (Operation > Contours).");
    return;
  }
  double length = 0;
  for (int i = 1; i < m_contourPoints.size(); i++) {
    QPointF d = m_contourPoints[i] - m_contourPoints[i - 1];
    length += std::hypot(d.x(), d.y());
  }

  int elemStart = findContainingElement(m_contourPoints.first());
  int elemEnd = findContainingElement(m_contourPoints.last());
  QString deltaAText = "n/a (endpoint outside mesh)";
  if (elemStart >= 0 && elemEnd >= 0) {
    std::complex<double> aStart = interpolateA(m_contourPoints.first(), elemStart);
    std::complex<double> aEnd = interpolateA(m_contourPoints.last(), elemEnd);
    std::complex<double> delta = aEnd - aStart;
    deltaAText = QString("%1, %2 (re, im)").arg(delta.real(), 0, 'g', 6).arg(delta.imag(), 0, 'g', 6);
  }

  QDialog dlg(this);
  dlg.setWindowTitle("Contour Properties");
  auto* form = new QFormLayout(&dlg);
  form->addRow("Points:", new QLabel(QString::number(m_contourPoints.size())));
  form->addRow("Length:", new QLabel(QString("%1").arg(length, 0, 'g', 6)));
  form->addRow("Delta A (end - start):", new QLabel(deltaAText));
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  form->addRow(buttons);
  appendOutput(QString("Contour: points=%1  length=%2  deltaA=%3")
                    .arg(m_contourPoints.size())
                    .arg(length, 0, 'g', 6)
                    .arg(deltaAText));
  dlg.exec();
}

void SolutionWindow::appendOutput(const QString& text)
{
  m_outputText->appendPlainText(text);
}

void SolutionWindow::onClearContourTriggered()
{
  m_contourPoints.clear();
  updateContourVisual();
  statusBar()->showMessage("Contour cleared.");
}

void SolutionWindow::onPlotXYTriggered()
{
  // Simplified relative to the classic GUI's own interactive XY chart --
  // no charting library is linked in this target (see femmqt/CMakeLists.txt),
  // so this samples |A| and |B| at evenly-spaced points along the current
  // contour and presents them as a plain table instead of a graphical
  // plot. Still gives the same underlying data, just not plotted visually.
  if (m_contourPoints.size() < 2) {
    QMessageBox::information(this, "Plot X-Y", "Draw a contour first (Operation > Contours).");
    return;
  }
  bool ok = false;
  int samples = QInputDialog::getInt(this, "Plot X-Y", "Number of sample points:", 20, 2, 500, 1, &ok);
  if (!ok)
    return;

  double totalLen = 0;
  QVector<double> segLen(m_contourPoints.size() - 1);
  for (int i = 1; i < m_contourPoints.size(); i++) {
    QPointF d = m_contourPoints[i] - m_contourPoints[i - 1];
    segLen[i - 1] = std::hypot(d.x(), d.y());
    totalLen += segLen[i - 1];
  }

  QString text = "arc length\tx\ty\t|A|\t|B|\n";
  for (int s = 0; s <= samples; s++) {
    double target = totalLen * s / samples;
    double acc = 0;
    QPointF pt = m_contourPoints.last();
    for (int i = 0; i < segLen.size(); i++) {
      if (target <= acc + segLen[i] || i == segLen.size() - 1) {
        double t = segLen[i] > 0 ? (target - acc) / segLen[i] : 0;
        pt = m_contourPoints[i] + qBound(0.0, t, 1.0) * (m_contourPoints[i + 1] - m_contourPoints[i]);
        break;
      }
      acc += segLen[i];
    }
    int elem = findContainingElement(pt);
    double aMag = 0, bMag = 0;
    if (elem >= 0) {
      std::complex<double> A = interpolateA(pt, elem);
      aMag = std::abs(A);
      const MeshSolutionElement& e = m_solution.elements[elem];
      bMag = std::hypot(std::hypot(e.B1re, e.B1im), std::hypot(e.B2re, e.B2im));
    }
    text += QString("%1\t%2\t%3\t%4\t%5\n").arg(target, 0, 'g', 6).arg(pt.x(), 0, 'g', 6).arg(pt.y(), 0, 'g', 6).arg(aMag, 0, 'g', 6).arg(bMag, 0, 'g', 6);
  }

  QDialog dlg(this);
  dlg.setWindowTitle("Plot X-Y along Contour");
  dlg.resize(500, 400);
  auto* layout = new QVBoxLayout(&dlg);
  auto* view = new QPlainTextEdit(text, &dlg);
  view->setReadOnly(true);
  view->setLineWrapMode(QPlainTextEdit::NoWrap);
  layout->addWidget(view);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  layout->addWidget(buttons);
  dlg.exec();
}

void SolutionWindow::onIntegrateTriggered()
{
  // femm.rc's "Integrate" is a standalone command distinct from "Finish
  // Contour" (which also shows the same result) -- kept as a thin alias
  // onto the same contour-integral logic rather than a second
  // implementation, since both operate on "the contour currently drawn."
  // The classic GUI's own Integrate additionally supports integrating
  // over an Area selection (energy, force, etc.) -- not implemented here
  // yet, since that needs per-element J/sigma data this app doesn't
  // currently extract from .ans (see the Areas tool's own simpler
  // area+avg-|B| scope).
  showContourIntegral();
}

void SolutionWindow::onReloadTriggered()
{
  if (m_currentPath.isEmpty()) {
    QMessageBox::information(this, "Reload", "No solution loaded.");
    return;
  }
  openAnsFile(m_currentPath);
}

void SolutionWindow::onProblemInfoTriggered()
{
  if (m_currentPath.isEmpty()) {
    QMessageBox::information(this, "Problem Info", "No solution loaded.");
    return;
  }
  FemmProblem problem;
  QString error;
  // .ans shares .fem's tag format for its header/property section (see
  // FemmFileIO.h) -- readFem happily parses that part and silently skips
  // the trailing [Solution] mesh data it doesn't recognize, so this is a
  // full second file read but a cheap one relative to actually parsing
  // the mesh (which is already loaded in m_solution anyway).
  if (!FemmFileIO::readFem(m_currentPath, problem, error)) {
    QMessageBox::warning(this, "Problem Info", error);
    return;
  }

  QDialog dlg(this);
  dlg.setWindowTitle("Problem Info");
  auto* form = new QFormLayout(&dlg);
  form->addRow("File:", new QLabel(m_currentPath));
  form->addRow("Frequency:", new QLabel(QString("%1 Hz").arg(problem.frequency, 0, 'g', 6)));
  form->addRow("Problem Type:", new QLabel(problem.problemType == FemmCoordinateType::Axisymmetric ? "Axisymmetric" : "Planar"));
  form->addRow("Depth:", new QLabel(QString::number(problem.depth, 'g', 6)));
  form->addRow("Precision:", new QLabel(QString::number(problem.precision, 'g', 3)));
  form->addRow("Materials:", new QLabel(QString::number(problem.materialProps.size())));
  form->addRow("Boundaries:", new QLabel(QString::number(problem.boundaryProps.size())));
  form->addRow("Circuits:", new QLabel(QString::number(problem.circuitProps.size())));
  form->addRow("Mesh nodes:", new QLabel(QString::number(m_solution.nodes.size())));
  form->addRow("Mesh elements:", new QLabel(QString::number(m_solution.elements.size())));
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  form->addRow(buttons);
  dlg.exec();
}

void SolutionWindow::onAboutTriggered()
{
  QMessageBox::about(this, "About FEMMX", "<b>FEMMX (Qt)</b> -- Solution Viewer<br><br>Density/Contour/Vector plots, Point/Contour/Area analysis tools.");
}

// Zoom/Pan -- see MainWindow::onZoomIn's equivalent comment; same
// relative-transform approach, just against this window's own view.
void SolutionWindow::onZoomIn()
{
  m_view->scale(2.0, 2.0);
  m_view->updateAntialiasingForScale();
}

void SolutionWindow::onZoomOut()
{
  m_view->scale(0.5, 0.5);
  m_view->updateAntialiasingForScale();
}

void SolutionWindow::onZoomNatural()
{
  if (m_item)
    m_view->fitInView(m_item->boundingRect(), Qt::KeepAspectRatio);
  m_view->updateAntialiasingForScale();
}

void SolutionWindow::onPanLeft()
{
  auto* bar = m_view->horizontalScrollBar();
  bar->setValue(bar->value() - m_view->viewport()->width() / 4);
}

void SolutionWindow::onPanRight()
{
  auto* bar = m_view->horizontalScrollBar();
  bar->setValue(bar->value() + m_view->viewport()->width() / 4);
}

void SolutionWindow::onPanUp()
{
  auto* bar = m_view->verticalScrollBar();
  bar->setValue(bar->value() - m_view->viewport()->height() / 4);
}

void SolutionWindow::onPanDown()
{
  auto* bar = m_view->verticalScrollBar();
  bar->setValue(bar->value() + m_view->viewport()->height() / 4);
}

void SolutionWindow::onCopyBitmapTriggered()
{
  if (!m_item) {
    QMessageBox::information(this, "Copy as Bitmap", "No solution loaded.");
    return;
  }
  QPixmap pixmap = m_view->viewport()->grab();
  QApplication::clipboard()->setPixmap(pixmap);
  statusBar()->showMessage("Copied view to clipboard as a bitmap.");
}

void SolutionWindow::onSwitchToClassicTriggered()
{
  if (m_currentPath.isEmpty()) {
    QMessageBox::information(this, "Switch GUI", "No solution loaded.");
    return;
  }
  GuiSwitch::writePreferredGui(GuiSwitch::PreferredGui::Classic);
  if (!GuiSwitch::launchClassicGui(m_currentPath)) {
    QMessageBox::warning(this, "Switch Failed", "Couldn't find or start femmx.exe next to femmqt.exe.");
    return;
  }
  close();
}

void SolutionWindow::addToRecentFiles(const QString& path)
{
  // Shares the same "recentFiles" QSettings key as MainWindow -- one
  // unified recent-files list across both windows this process can open,
  // rather than two independently-tracked ones a user would need to
  // remember apart.
  QSettings settings;
  QStringList recent = settings.value("recentFiles").toStringList();
  recent.removeAll(path);
  recent.prepend(path);
  while (recent.size() > 8)
    recent.removeLast();
  settings.setValue("recentFiles", recent);
  updateRecentFilesMenu();
}

void SolutionWindow::updateRecentFilesMenu()
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
    connect(action, &QAction::triggered, this, &SolutionWindow::onOpenRecentFile);
  }
}

void SolutionWindow::onOpenRecentFile()
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
  // A recent-files entry might be a .fem (geometry, from MainWindow's own
  // shared list) rather than a .ans/.ansx -- route it back to a geometry
  // editor window instead of trying to open it here.
  QString suffix = QFileInfo(path).suffix();
  if (suffix.compare("ans", Qt::CaseInsensitive) != 0 && suffix.compare("ansx", Qt::CaseInsensitive) != 0) {
    auto* window = new MainWindow();
    window->show();
    window->openFile(path);
    return;
  }
  openAnsFile(path);
}

void SolutionWindow::onHelpTopicsTriggered()
{
  // See MainWindow::onHelpTopicsTriggered's identical reasoning.
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

void SolutionWindow::onLicenseTriggered()
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
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  layout->addWidget(buttons);
  dlg.exec();
}
