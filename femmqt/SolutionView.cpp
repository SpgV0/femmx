#include "SolutionView.h"

#include "AnsFileIO.h"
#include "AnsxFileIO.h"
#include "FemmProblem.h"

#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QStatusBar>

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

void MeshSolutionItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
  if (!m_solution || m_solution->elements.isEmpty())
    return;

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

SolutionWindow::SolutionWindow(QWidget* parent)
    : QMainWindow(parent)
{
  setWindowTitle("FEMMX (Qt) - Solution Viewer");
  resize(1024, 768);

  m_scene = new QGraphicsScene(this);
  m_view = new QGraphicsView(m_scene, this);
  m_view->setRenderHint(QPainter::Antialiasing, false); // large meshes: skip AA for speed
  // Matches MainWindow's view->scale(1,-1): .ans geometry is in the same
  // math (y-up) convention as .fem.
  m_view->scale(1, -1);
  setCentralWidget(m_view);

  QMenu* fileMenu = menuBar()->addMenu("&File");
  fileMenu->addAction("&Open Solution...", this, &SolutionWindow::onOpenTriggered, QKeySequence::Open);

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
  m_item = new MeshSolutionItem(&m_solution);
  m_scene->addItem(m_item);
  m_view->fitInView(m_item->boundingRect(), Qt::KeepAspectRatio);

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
