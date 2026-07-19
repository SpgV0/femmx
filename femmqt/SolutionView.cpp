#include "SolutionView.h"

#include "AnsFileIO.h"
#include "AnsxFileIO.h"
#include "FemmFileIO.h"
#include "FemmProblem.h"

#include <QActionGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
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
#include <QStatusBar>
#include <QToolBar>
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

void MeshSolutionItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
  if (!m_solution || m_solution->elements.isEmpty())
    return;
  switch (m_mode) {
  case PlotMode::Density: paintDensity(painter); break;
  case PlotMode::Contour: paintContour(painter); break;
  case PlotMode::Vector: paintVector(painter); break;
  }
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
    double bMag = std::hypot(std::hypot(e.B1re, e.B1im), std::hypot(e.B2re, e.B2im));
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

  QPen pen(Qt::black);
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

  QPen pen(Qt::black);
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
  event->accept();
}

SolutionWindow::SolutionWindow(QWidget* parent)
    : QMainWindow(parent)
{
  setWindowTitle("FEMMX (Qt) - Solution Viewer");
  resize(1024, 768);

  m_scene = new QGraphicsScene(this);
  m_view = new SolutionGraphicsView(m_scene, this);
  m_view->setRenderHint(QPainter::Antialiasing, false); // large meshes: skip AA for speed
  // Matches MainWindow's view->scale(1,-1): .ans geometry is in the same
  // math (y-up) convention as .fem.
  m_view->scale(1, -1);
  setCentralWidget(m_view);
  connect(m_view, &SolutionGraphicsView::clickedAt, this, &SolutionWindow::onCanvasClicked);

  QMenu* fileMenu = menuBar()->addMenu("&File");
  fileMenu->addAction("&Open Solution...", this, &SolutionWindow::onOpenTriggered, QKeySequence::Open);

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
  viewMenu->addSeparator();
  viewMenu->addAction("Problem &Info...", this, &SolutionWindow::onProblemInfoTriggered);

  // Matches femm.rc's post-processor "Operation" menu (Point properties /
  // Contours / Areas) -- Plot X-Y and Integrate are folded into the
  // contour tool's own result dialog here rather than being separate
  // modal commands, since they both operate on "the contour just drawn."
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
  opMenu->addAction("&Plot X-Y along Contour...", this, &SolutionWindow::onPlotXYTriggered);

  QMenu* helpMenu = menuBar()->addMenu("&Help");
  helpMenu->addAction("&About FEMMX...", this, &SolutionWindow::onAboutTriggered);

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
  if (m_contourPoints.size() < 2) {
    QMessageBox::information(this, "Finish Contour", "Click at least two points first.");
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
  dlg.exec();
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
