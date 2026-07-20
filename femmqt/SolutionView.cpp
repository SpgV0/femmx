#include "SolutionView.h"

#include "AnsFileIO.h"
#include "AppPreferences.h"
#include "AppTheme.h"
#include "BHCurveDialog.h"
#include "CircuitAnalysis.h"
#include "AnsxFileIO.h"
#include "FemmFileIO.h"
#include "FemmProblem.h"
#include "GuiSwitch.h"
#include "HoverTooltip.h"
#include "IconTheme.h"
#include "MainWindow.h"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#ifdef FEMMQT_HAVE_OPENGL
#include <QOpenGLWidget>
#endif
#include <QPageSetupDialog>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
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

// Matches femm/CircDlg.cpp's CComplex::ToStringAlt display convention
// closely enough for this dialog's purposes: a bare number for a
// (numerically) real value, "re + jim" otherwise.
QString complexToString(std::complex<double> z)
{
  if (std::abs(z.imag()) < 1e-12 * std::max(1.0, std::abs(z.real())))
    return QString::number(z.real(), 'g', 6);
  return QString("%1 %2 j%3").arg(QString::number(z.real(), 'g', 6), z.imag() < 0 ? "-" : "+", QString::number(std::abs(z.imag()), 'g', 6));
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

  if (!solution->nodes.isEmpty()) {
    m_aMin = m_aMax = solution->nodes[0].Are;
    for (const MeshSolutionNode& n : solution->nodes) {
      m_aMin = std::min(m_aMin, n.Are);
      m_aMax = std::max(m_aMax, n.Are);
    }
  }

  // Precompute each node's average value (across every element touching
  // it -- see QuantityData's header comment for why) plus the min/max
  // range, for every DensityQuantity at once. Done once here rather than
  // per-paint or per-quantity-switch, since paint() can run many times
  // (every pan/zoom) but the mesh itself never changes -- the whole
  // point of this precompute pass, same as the original |B|-only version.
  for (int qi = 0; qi < 6; qi++) {
    auto q = static_cast<DensityQuantity>(qi);
    QuantityData& qd = m_quantityData[qi];
    qd.nodeAvg.fill(0.0, solution->nodes.size());
    QVector<int> touchCount(solution->nodes.size(), 0);
    bool first = true;
    for (const MeshSolutionElement& e : solution->elements) {
      double v = elementQuantity(e, q);
      if (first) {
        qd.vMin = qd.vMax = v;
        first = false;
      } else {
        qd.vMin = std::min(qd.vMin, v);
        qd.vMax = std::max(qd.vMax, v);
      }
      for (int p : { e.p0, e.p1, e.p2 }) {
        if (p >= 0 && p < qd.nodeAvg.size()) {
          qd.nodeAvg[p] += v;
          touchCount[p]++;
        }
      }
    }
    for (int i = 0; i < qd.nodeAvg.size(); i++)
      if (touchCount[i] > 0)
        qd.nodeAvg[i] /= touchCount[i];
  }
  m_lastDensityLo = m_quantityData[static_cast<int>(m_densityQuantity)].vMin;
  m_lastDensityHi = m_quantityData[static_cast<int>(m_densityQuantity)].vMax;

  buildSpatialIndex();
}

void MeshSolutionItem::buildSpatialIndex()
{
  m_spatialIndex = SpatialIndex();
  if (!m_solution || m_solution->elements.isEmpty() || m_solution->nodes.isEmpty())
    return;

  double w = std::max(m_bounds.width(), 1e-12);
  double h = std::max(m_bounds.height(), 1e-12);
  // Aim for roughly one element per cell on average, same heuristic as
  // SolutionWindow::buildSpatialIndex.
  double cellSize = std::sqrt((w * h) / std::max(1, (int)m_solution->elements.size()));
  if (!(cellSize > 0))
    cellSize = std::max(w, h);
  int cols = std::max(1, (int)(w / cellSize) + 1);
  int rows = std::max(1, (int)(h / cellSize) + 1);

  m_spatialIndex.minX = m_bounds.left();
  m_spatialIndex.minY = m_bounds.top();
  m_spatialIndex.cellSize = cellSize;
  m_spatialIndex.cols = cols;
  m_spatialIndex.rows = rows;
  m_spatialIndex.cells.resize(cols * rows);

  for (int i = 0; i < m_solution->elements.size(); i++) {
    const MeshSolutionElement& e = m_solution->elements[i];
    if (e.p0 < 0 || e.p0 >= m_solution->nodes.size() || e.p1 < 0 || e.p1 >= m_solution->nodes.size() || e.p2 < 0 || e.p2 >= m_solution->nodes.size())
      continue;
    const MeshSolutionNode& n0 = m_solution->nodes[e.p0];
    const MeshSolutionNode& n1 = m_solution->nodes[e.p1];
    const MeshSolutionNode& n2 = m_solution->nodes[e.p2];
    double triMinX = std::min({ n0.x, n1.x, n2.x });
    double triMaxX = std::max({ n0.x, n1.x, n2.x });
    double triMinY = std::min({ n0.y, n1.y, n2.y });
    double triMaxY = std::max({ n0.y, n1.y, n2.y });
    int c0 = std::clamp((int)std::floor((triMinX - m_spatialIndex.minX) / cellSize), 0, cols - 1);
    int c1 = std::clamp((int)std::floor((triMaxX - m_spatialIndex.minX) / cellSize), 0, cols - 1);
    int r0 = std::clamp((int)std::floor((triMinY - m_spatialIndex.minY) / cellSize), 0, rows - 1);
    int r1 = std::clamp((int)std::floor((triMaxY - m_spatialIndex.minY) / cellSize), 0, rows - 1);
    for (int r = r0; r <= r1; r++)
      for (int c = c0; c <= c1; c++)
        m_spatialIndex.cells[r * cols + c].push_back(i);
  }

  m_visitedMark.fill(-1, m_solution->elements.size());
  m_visitedGen = 0;
}

QVector<int> MeshSolutionItem::elementsOverlapping(const QRectF& rect) const
{
  QVector<int> result;
  if (m_spatialIndex.cols == 0 || m_spatialIndex.rows == 0)
    return result;
  int cols = m_spatialIndex.cols, rows = m_spatialIndex.rows;
  double cellSize = m_spatialIndex.cellSize;
  int c0 = std::clamp((int)std::floor((rect.left() - m_spatialIndex.minX) / cellSize), 0, cols - 1);
  int c1 = std::clamp((int)std::floor((rect.right() - m_spatialIndex.minX) / cellSize), 0, cols - 1);
  int r0 = std::clamp((int)std::floor((rect.top() - m_spatialIndex.minY) / cellSize), 0, rows - 1);
  int r1 = std::clamp((int)std::floor((rect.bottom() - m_spatialIndex.minY) / cellSize), 0, rows - 1);
  m_visitedGen++;
  for (int r = r0; r <= r1; r++) {
    for (int c = c0; c <= c1; c++) {
      for (int idx : m_spatialIndex.cells[r * cols + c]) {
        if (m_visitedMark[idx] != m_visitedGen) {
          m_visitedMark[idx] = m_visitedGen;
          result.push_back(idx);
        }
      }
    }
  }
  return result;
}

double MeshSolutionItem::elementQuantity(const MeshSolutionElement& e, DensityQuantity q) const
{
  switch (q) {
  case DensityQuantity::BMag: return std::hypot(std::hypot(e.B1re, e.B1im), std::hypot(e.B2re, e.B2im));
  case DensityQuantity::BReMag: return std::hypot(e.B1re, e.B2re);
  case DensityQuantity::BImMag: return std::hypot(e.B1im, e.B2im);
  case DensityQuantity::LogBMag: return std::log10(std::max(std::hypot(std::hypot(e.B1re, e.B1im), std::hypot(e.B2re, e.B2im)), 1e-300));
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
  // H = B/(mu_r*mu0) per axis (femm/FemmviewDoc.cpp's GetH, muo =
  // 1.2566370614359173e-6 H/m matching femm/StdAfx.h's own constant
  // exactly rather than a recomputed 4*pi*1e-7, which can differ in the
  // last bit or two). Exact for linear materials only -- see this
  // method's declaration in SolutionView.h for the nonlinear/laminated/
  // incremental-permeability cases not covered.
  case DensityQuantity::HMag: {
    constexpr double kMuo = 1.2566370614359173e-6;
    double h1re = e.B1re / (e.muX * kMuo), h1im = e.B1im / (e.muX * kMuo);
    double h2re = e.B2re / (e.muY * kMuo), h2im = e.B2im / (e.muY * kMuo);
    return std::hypot(std::hypot(h1re, h1im), std::hypot(h2re, h2im));
  }
  // jRe/jIm are precomputed once (AnsFileIO::readAns) -- see
  // MeshSolutionElement's comment for why (needs nodal A, not available
  // from a single element at paint time).
  case DensityQuantity::JMag: return std::hypot(e.jRe, e.jIm);
  }
  return 0;
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

void MeshSolutionItem::setDensityQuantity(DensityQuantity q)
{
  m_densityQuantity = q;
  // Reset to this quantity's global range immediately -- otherwise the
  // legend would briefly show the PREVIOUS quantity's cached local range
  // (wrong units/scale entirely) until the next paintDensity() call
  // overwrites it.
  m_lastDensityLo = m_quantityData[static_cast<int>(q)].vMin;
  m_lastDensityHi = m_quantityData[static_cast<int>(q)].vMax;
  update();
}

int MeshSolutionItem::legendBandCount()
{
  return kNumBands;
}

QColor MeshSolutionItem::legendBandColor(int band)
{
  return bandColor(band);
}

void MeshSolutionItem::legendRange(double& lo, double& hi) const
{
  lo = m_lastDensityLo;
  hi = m_lastDensityHi;
}

QString MeshSolutionItem::legendTitle() const
{
  switch (m_densityQuantity) {
  case DensityQuantity::BMag: return "|B|, Tesla";
  case DensityQuantity::BReMag: return "|B_re|, Tesla";
  case DensityQuantity::BImMag: return "|B_im|, Tesla";
  case DensityQuantity::LogBMag: return "log10(|B|), log(Tesla)";
  case DensityQuantity::HMag: return "|H|, Amp/m";
  case DensityQuantity::JMag: return "|Js+Je|, MA/m^2";
  }
  return QString();
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

void MeshSolutionItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
  if (!m_solution || m_solution->elements.isEmpty())
    return;
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // option->exposedRect alone is NOT "what's currently visible" -- confirmed
  // directly via logging: after a pan/zoom (a transform change, which is
  // most interactive use), Qt marks the WHOLE item dirty, so exposedRect is
  // the item's entire boundingRect() regardless of how far zoomed in the
  // view actually is. That silently defeated the exposed-rect viewport
  // culling below during real interactive zooming (it visually looked
  // right -- drawing extra off-screen triangles is harmless -- so this
  // went unnoticed until the zoom-adaptive density-range feature made it
  // observable: the legend never rescaled because paintDensity always saw
  // the full mesh, not what was on screen). `widget` is this item's
  // viewport; its parent is the owning QGraphicsView, which can map its
  // own visible rect() back to scene coordinates -- intersecting that with
  // exposedRect gives the actual visible region regardless of why this
  // paint() call was triggered.
  QRectF visibleRect = m_bounds;
  if (widget) {
    if (auto* view = qobject_cast<QGraphicsView*>(widget->parentWidget()))
      visibleRect = view->mapToScene(view->viewport()->rect()).boundingRect();
  }
  QRectF exposedRect = option ? option->exposedRect.intersected(visibleRect) : visibleRect;
  switch (m_mode) {
  case PlotMode::Density: paintDensity(painter, exposedRect); break;
  case PlotMode::Contour: paintContour(painter, exposedRect); break;
  case PlotMode::Vector: paintVector(painter, exposedRect); break;
  }
  if (m_showMesh || m_showPoints)
    paintMeshOverlay(painter, exposedRect);
}

void MeshSolutionItem::paintDensity(QPainter* painter, const QRectF& exposedRect)
{
  const QuantityData& qd = m_quantityData[static_cast<int>(m_densityQuantity)];

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20: per
  // user request ("when zooming into a very detailed model, the field
  // definition improves in the density plot, for example litz wire") --
  // banding used to always span the WHOLE MESH's global min/max, so
  // zooming into a small region whose own value range is a tiny fraction
  // of the global range (a fine-featured area like litz wire strands next
  // to a saturated core, say) rendered as one or two flat colors no matter
  // how far in you zoomed -- all the color resolution was "spent" on the
  // full-mesh range, none of it on what's actually on screen. First pass
  // below finds min/max over only the currently-visible (exposedRect-
  // overlapping) elements and bands against THAT instead, so the 20 bands
  // always cover whatever's in view -- more of the mesh visible (zoomed
  // out) means coarser per-band detail, exactly like before; a small
  // zoomed-in region gets the full 20-band resolution to itself. Falls
  // back to the global range when nothing's visible or the local range is
  // degenerate (span <= 0, e.g. a single visible element). Cached into
  // m_lastDensityLo/Hi so the legend (a separate QWidget, not part of this
  // item's own scene-space paint) can report the same range it's actually
  // looking at -- see SolutionGraphicsView::scrollContentsBy for why the
  // legend repaints on every pan/zoom to keep picking that up.
  double lo = qd.vMin, hi = qd.vMax;
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20: was
  // a linear scan over m_solution->elements with a per-element bbox
  // reject -- correct, but O(total mesh size) every paint regardless of
  // zoom. elementsOverlapping() does the same bbox test but only against
  // elements the spatial index's cell range says can possibly overlap,
  // so cost now tracks what's on screen. The precise per-element bbox
  // check below is kept as a cheap correctness backstop (a cell can hold
  // elements whose bbox pokes into it but not into the exact query rect).
  QVector<int> visible = elementsOverlapping(exposedRect);
  {
    bool first = true;
    for (int ei : visible) {
      const MeshSolutionElement& e = m_solution->elements[ei];
      if (e.p0 < 0 || e.p0 >= m_solution->nodes.size() || e.p1 < 0 || e.p1 >= m_solution->nodes.size() || e.p2 < 0 || e.p2 >= m_solution->nodes.size())
        continue;
      const MeshSolutionNode& n0 = m_solution->nodes[e.p0];
      const MeshSolutionNode& n1 = m_solution->nodes[e.p1];
      const MeshSolutionNode& n2 = m_solution->nodes[e.p2];
      double triMinX = std::min({ n0.x, n1.x, n2.x });
      double triMaxX = std::max({ n0.x, n1.x, n2.x });
      double triMinY = std::min({ n0.y, n1.y, n2.y });
      double triMaxY = std::max({ n0.y, n1.y, n2.y });
      if (triMaxX < exposedRect.left() || triMinX > exposedRect.right() || triMaxY < exposedRect.top() || triMinY > exposedRect.bottom())
        continue;
      double v = m_smooth
          ? (qd.nodeAvg[e.p0] + qd.nodeAvg[e.p1] + qd.nodeAvg[e.p2]) / 3.0
          : elementQuantity(e, m_densityQuantity);
      if (first) {
        lo = hi = v;
        first = false;
      } else {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
      }
    }
    if (first || hi <= lo) {
      lo = qd.vMin;
      hi = qd.vMax;
    }
  }
  m_lastDensityLo = lo;
  m_lastDensityHi = hi;
  double bMin = lo, bMax = hi;
  double span = bMax - bMin;

  // One QPainterPath per color band, filled with a single fillPath() call
  // each -- O(kNumBands) draw calls regardless of element count, the same
  // batching principle as PolyPolygon()/FlushDensityBand in the MFC GUI.
  QPainterPath bandPaths[kNumBands];

  for (int ei : visible) {
    const MeshSolutionElement& e = m_solution->elements[ei];
    if (e.p0 < 0 || e.p0 >= m_solution->nodes.size() || e.p1 < 0 || e.p1 >= m_solution->nodes.size() || e.p2 < 0 || e.p2 >= m_solution->nodes.size())
      continue;

    const MeshSolutionNode& n0 = m_solution->nodes[e.p0];
    const MeshSolutionNode& n1 = m_solution->nodes[e.p1];
    const MeshSolutionNode& n2 = m_solution->nodes[e.p2];

    // Viewport culling: skip any triangle that doesn't overlap the
    // currently-visible region at all -- see this method's declaration
    // in SolutionView.h for why this matters far more than GPU-vs-CPU
    // rasterization for a huge mesh. Cheap bounding-box check, done
    // before the (comparatively expensive) quantity/band lookup below.
    double triMinX = std::min({ n0.x, n1.x, n2.x });
    double triMaxX = std::max({ n0.x, n1.x, n2.x });
    double triMinY = std::min({ n0.y, n1.y, n2.y });
    double triMaxY = std::max({ n0.y, n1.y, n2.y });
    if (triMaxX < exposedRect.left() || triMinX > exposedRect.right() || triMaxY < exposedRect.top() || triMinY > exposedRect.bottom())
      continue;

    // Smooth: band on the average of the 3 corners' node-averaged value
    // instead of this element's own single value -- see QuantityData's
    // header comment.
    double bMag = m_smooth
        ? (qd.nodeAvg[e.p0] + qd.nodeAvg[e.p1] + qd.nodeAvg[e.p2]) / 3.0
        : elementQuantity(e, m_densityQuantity);
    int band = (span > 0) ? (int)((bMag - bMin) / span * kNumBands) : 0;
    if (band >= kNumBands)
      band = kNumBands - 1;
    if (band < 0)
      band = 0;

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

void MeshSolutionItem::paintContour(QPainter* painter, const QRectF& exposedRect)
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
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // Are's global min/max never changes once a solution is loaded (unlike
  // paintDensity's local range, contour LEVELS are deliberately fixed to
  // the whole mesh's range regardless of zoom, so lines mean the same
  // potential wherever you scroll to) -- precomputed once in the
  // constructor instead of rescanning every node on every single
  // paintContour() call (every frame Contour mode is active and the view
  // pans/zooms).
  double aMin = m_aMin, aMax = m_aMax;
  double span = aMax - aMin;
  if (span <= 0)
    return;

  constexpr int kNumLevels = 20;
  QPainterPath path;
  for (int ei : elementsOverlapping(exposedRect)) {
    const MeshSolutionElement& e = m_solution->elements[ei];
    if (e.p0 < 0 || e.p0 >= m_solution->nodes.size() || e.p1 < 0 || e.p1 >= m_solution->nodes.size() || e.p2 < 0 || e.p2 >= m_solution->nodes.size())
      continue;
    const MeshSolutionNode& n0 = m_solution->nodes[e.p0];
    const MeshSolutionNode& n1 = m_solution->nodes[e.p1];
    const MeshSolutionNode& n2 = m_solution->nodes[e.p2];

    // See paintDensity's identical check for why.
    double triMinX = std::min({ n0.x, n1.x, n2.x });
    double triMaxX = std::max({ n0.x, n1.x, n2.x });
    double triMinY = std::min({ n0.y, n1.y, n2.y });
    double triMaxY = std::max({ n0.y, n1.y, n2.y });
    if (triMaxX < exposedRect.left() || triMinX > exposedRect.right() || triMaxY < exposedRect.top() || triMinY > exposedRect.bottom())
      continue;

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

void MeshSolutionItem::paintVector(QPainter* painter, const QRectF& exposedRect)
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
  // Arrows extend up to arrowLen beyond the centroid, so grow the cull
  // rect by that much rather than culling against the bare centroid --
  // otherwise an arrow whose centroid is just outside the exposed rect
  // but whose visible tip pokes into it would be skipped.
  QRectF cullRect = exposedRect.adjusted(-arrowLen, -arrowLen, arrowLen, arrowLen);
  for (int i = 0; i < m_solution->elements.size(); i += stride) {
    const MeshSolutionElement& e = m_solution->elements[i];
    if (!cullRect.contains(e.ctrX, e.ctrY))
      continue;
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

void MeshSolutionItem::paintMeshOverlay(QPainter* painter, const QRectF& exposedRect)
{
  // Drawn on top of whichever plot mode is active (femm.rc's Show Mesh/
  // Show Points are independent toggles, not plot modes of their own).
  if (m_showMesh) {
    QPainterPath path;
    for (int ei : elementsOverlapping(exposedRect)) {
      const MeshSolutionElement& e = m_solution->elements[ei];
      if (e.p0 < 0 || e.p0 >= m_solution->nodes.size() || e.p1 < 0 || e.p1 >= m_solution->nodes.size() || e.p2 < 0 || e.p2 >= m_solution->nodes.size())
        continue;
      const MeshSolutionNode& n0 = m_solution->nodes[e.p0];
      const MeshSolutionNode& n1 = m_solution->nodes[e.p1];
      const MeshSolutionNode& n2 = m_solution->nodes[e.p2];

      // See paintDensity's identical check for why.
      double triMinX = std::min({ n0.x, n1.x, n2.x });
      double triMaxX = std::max({ n0.x, n1.x, n2.x });
      double triMinY = std::min({ n0.y, n1.y, n2.y });
      double triMaxY = std::max({ n0.y, n1.y, n2.y });
      if (triMaxX < exposedRect.left() || triMinX > exposedRect.right() || triMaxY < exposedRect.top() || triMinY > exposedRect.bottom())
        continue;

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
    QRectF cullRect = exposedRect.adjusted(-r, -r, r, r);
    for (const MeshSolutionNode& n : m_solution->nodes) {
      if (!cullRect.contains(n.x, n.y))
        continue;
      painter->drawEllipse(QPointF(n.x, n.y), r, r);
    }
  }
}

// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
// color-band legend overlay ("add a colourmap bar on the side, similar to
// old gui"), matching femm/FemmviewView.cpp's own Density Plot legend --
// a fixed-size color-swatch stack with a numeric range label per band,
// pinned to the viewport's top-right corner. Deliberately a plain QWidget
// child of the viewport (same pattern as m_cursorTooltip just below) and
// NOT part of MeshSolutionItem::paint(): that method draws in SCENE space
// (pans/zooms with the model), while the legend -- like the classic GUI's
// own, drawn straight onto the device context in FemmviewView::OnDraw --
// must stay fixed to the viewport regardless of pan/zoom.
class SolutionLegendWidget : public QWidget {
  public:
  explicit SolutionLegendWidget(QWidget* parent)
      : QWidget(parent)
  {
    setAttribute(Qt::WA_TransparentForMouseEvents);
  }

  void setItem(MeshSolutionItem* item)
  {
    m_item = item;
    updateGeometry();
  }

  // Recomputes this widget's fixed size from the current item's state --
  // called whenever plot mode/quantity changes, since the title text
  // (and therefore the widest row) can change length.
  void updateGeometry()
  {
    if (!m_item) {
      resize(0, 0);
      return;
    }
    QFontMetrics fm(font());
    int titleWidth = fm.horizontalAdvance(m_item->legendTitle());
    int rowWidth = fm.horizontalAdvance("<1.234e+00 : 1.234e+00");
    int w = std::max({ titleWidth, rowWidth, 160 }) + kSwatchWidth + kMargin * 3;
    int bandCount = MeshSolutionItem::legendBandCount();
    int h = kMargin * 2 + bandCount * kRowHeight + kTitleHeight;
    resize(w, h);
  }

  protected:
  void paintEvent(QPaintEvent*) override
  {
    if (!m_item)
      return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    painter.fillRect(rect(), palette().color(QPalette::Window));
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    double lo, hi;
    m_item->legendRange(lo, hi);
    int bandCount = MeshSolutionItem::legendBandCount();
    double span = (hi - lo) / bandCount;

    painter.setPen(palette().color(QPalette::WindowText));
    int y = kMargin;
    // Band 0 (lowest value) at the bottom, highest at the top -- matches
    // how the color ramp reads bottom-to-top on a typical legend/colorbar.
    for (int row = 0; row < bandCount; row++) {
      int band = bandCount - 1 - row;
      QRect swatch(kMargin, y, kSwatchWidth, kRowHeight - 1);
      painter.fillRect(swatch, MeshSolutionItem::legendBandColor(band));
      double bandLo = lo + band * span;
      double bandHi = lo + (band + 1) * span;
      QString label = band == bandCount - 1
          ? QString(">%1").arg(bandLo, 0, 'e', 3)
          : (band == 0 ? QString("<%1").arg(bandHi, 0, 'e', 3)
                       : QString("%1:%2").arg(bandLo, 0, 'e', 2).arg(bandHi, 0, 'e', 2));
      painter.drawText(QRect(kMargin * 2 + kSwatchWidth, y, width() - kSwatchWidth - kMargin * 3, kRowHeight - 1),
          Qt::AlignVCenter | Qt::AlignLeft, label);
      y += kRowHeight;
    }
    painter.drawText(QRect(kMargin, y, width() - kMargin * 2, kTitleHeight), Qt::AlignVCenter | Qt::AlignLeft, m_item->legendTitle());
  }

  private:
  static constexpr int kMargin = 6;
  static constexpr int kSwatchWidth = 22;
  static constexpr int kRowHeight = 15;
  static constexpr int kTitleHeight = 20;
  MeshSolutionItem* m_item = nullptr;
};

SolutionGraphicsView::SolutionGraphicsView(QGraphicsScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
{
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  setResizeAnchor(QGraphicsView::AnchorUnderMouse);
  setMouseTracking(true); // needed for hoveredAt() to fire without a button held

#ifdef FEMMQT_HAVE_OPENGL
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // per user request to prioritize pan/zoom responsiveness on large
  // meshes -- a QOpenGLWidget viewport moves rasterization (this view's
  // dominant per-frame cost: filling potentially millions of density-plot
  // triangles) onto the GPU instead of QPainter's software raster
  // backend. Set BEFORE creating m_cursorTooltip/m_legend below: they're
  // plain QWidget children of viewport(), and setViewport() destroys
  // whatever widget was there before, which would otherwise orphan them.
  // Qt composites ordinary QWidget children over a QOpenGLWidget's FBO
  // output automatically (standard Qt >= 5.4 behavior), so the tooltip
  // and legend still work unchanged on top of the GL-rendered mesh.
  setViewport(new QOpenGLWidget(this));
#endif

  m_cursorTooltip = new QLabel(viewport());
  m_cursorTooltip->setStyleSheet(
      "QLabel { background-color: rgba(20, 20, 20, 200); color: white; "
      "padding: 2px 5px; border-radius: 3px; font-size: 11px; }");
  m_cursorTooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_cursorTooltip->hide();

  m_legend = new SolutionLegendWidget(viewport());
  m_legend->hide();
}

void SolutionGraphicsView::setLegendItem(MeshSolutionItem* item)
{
  m_legendItem = item;
  m_legend->setItem(item);
  refreshLegend();
}

void SolutionGraphicsView::setLegendVisible(bool visible)
{
  m_legendEnabled = visible;
  refreshLegend();
}

void SolutionGraphicsView::refreshLegend()
{
  bool visible = m_legendEnabled && m_legendItem && m_legendItem->plotMode() == MeshSolutionItem::PlotMode::Density;
  if (!visible) {
    m_legend->hide();
    return;
  }
  m_legend->updateGeometry();
  m_legend->move(viewport()->width() - m_legend->width() - 8, 8);
  m_legend->show();
  m_legend->raise();
  m_legend->update();
}

void SolutionGraphicsView::resizeEvent(QResizeEvent* event)
{
  QGraphicsView::resizeEvent(event);
  refreshLegend();
}

void SolutionGraphicsView::scrollContentsBy(int dx, int dy)
{
  QGraphicsView::scrollContentsBy(dx, dy);
  // Just a repaint (not refreshLegend()'s visibility recheck) -- mode/
  // quantity/toggle state hasn't changed, only what the legend's numbers
  // should read given the newly-visible region. The upcoming viewport
  // repaint this scroll also triggers will have already re-run
  // paintDensity() and refreshed MeshSolutionItem's cached local range by
  // the time this widget's own paint event actually executes.
  if (m_legend->isVisible())
    m_legend->update();
}

void SolutionGraphicsView::mousePressEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton)
    emit clickedAt(mapToScene(event->pos()));
  QGraphicsView::mousePressEvent(event);
}

void SolutionGraphicsView::mouseMoveEvent(QMouseEvent* event)
{
  QGraphicsView::mouseMoveEvent(event);

  // Position tracks every move (cheap); the text is refreshed separately
  // by setTooltipText(), called from SolutionWindow::onCanvasHovered
  // once it's finished its own (throttled -- a mesh-element lookup, not
  // free) field-value computation for this same position.
  QRect oldGeometry = m_cursorTooltip->geometry();
  QPoint pos = event->pos() + QPoint(16, 16);
  pos.setX(std::min(pos.x(), viewport()->width() - m_cursorTooltip->width()));
  pos.setY(std::min(pos.y(), viewport()->height() - m_cursorTooltip->height()));
  m_cursorTooltip->move(pos);
  m_cursorTooltip->show();
  m_cursorTooltip->raise();
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // was viewport()->update(oldGeometry) -- confirmed via a real installed
  // build that this does NOT reliably erase the tooltip's old position,
  // leaving a visible trail of stale copies as the cursor moves. Root
  // cause: this view's default MinimalViewportUpdate mode tracks dirty
  // regions from SCENE changes, not from a plain QWidget child (the
  // tooltip) moving -- a bare viewport()->update() call on that region
  // can get treated as a no-op since nothing in the scene itself
  // "changed" there. GeometryView.cpp's editor fixes the equivalent bug
  // with FullViewportUpdate, but that's not an option here: this view
  // can hold millions of mesh elements, and forcing a full repaint on
  // every single mouse-move would make the reported "everything is slow
  // on large geometries" complaint significantly worse. scene()->
  // invalidate() is the mechanism MinimalViewportUpdate actually
  // respects for "redraw this region even though nothing scene-side
  // changed" -- correctly limited to just the vacated rect, not the
  // whole viewport.
  scene()->invalidate(mapToScene(oldGeometry).boundingRect());

  emit hoveredAt(mapToScene(event->pos()));
}

void SolutionGraphicsView::leaveEvent(QEvent* event)
{
  m_cursorTooltip->hide();
  QGraphicsView::leaveEvent(event);
}

void SolutionGraphicsView::setTooltipText(const QString& text)
{
  m_cursorTooltip->setText(text);
  m_cursorTooltip->adjustSize();
}

void SolutionGraphicsView::wheelEvent(QWheelEvent* event)
{
  double factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
  scale(factor, factor);
  updateAntialiasingForScale();
  // Belt-and-suspenders alongside scrollContentsBy: scale() normally
  // triggers it too (see that override's comment), but this guarantees
  // the legend picks up the new visible range on every wheel-zoom step
  // regardless.
  if (m_legend && m_legend->isVisible())
    m_legend->update();
  event->accept();
}

void SolutionGraphicsView::updateAntialiasingForScale()
{
  // Was zoom-gated (only on past 8x) -- see the class declaration's
  // comment in SolutionView.h for why that's no longer the case.
  // Unconditional now; kept as its own function (rather than inlining
  // setRenderHint calls at each call site) so a future element-count-
  // based heuristic has one place to live.
  setRenderHint(QPainter::Antialiasing, true);
}

SolutionWindow::SolutionWindow(QWidget* parent)
    : QMainWindow(parent)
{
  setWindowTitle("FEMMX (Qt) - Solution Viewer");
  resize(1024, 768);

  m_scene = new QGraphicsScene(this);
  m_scene->setBackgroundBrush(AppTheme::background());
  m_view = new SolutionGraphicsView(m_scene, this);
  m_view->setRenderHint(QPainter::Antialiasing, true); // see updateAntialiasingForScale()
  // Matches MainWindow's view->scale(1,-1): .ans geometry is in the same
  // math (y-up) convention as .fem.
  m_view->scale(1, -1);
  setCentralWidget(m_view);
  connect(m_view, &SolutionGraphicsView::clickedAt, this, &SolutionWindow::onCanvasClicked);
  connect(m_view, &SolutionGraphicsView::hoveredAt, this, &SolutionWindow::onCanvasHovered);

  m_positionLabel = new QLabel(this);
  m_positionLabel->setMinimumWidth(360);
  statusBar()->addPermanentWidget(m_positionLabel);

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
  fileMenu->addAction("Print Pre&view...", this, &SolutionWindow::onPrintPreviewTriggered);
  fileMenu->addAction("&Print...", this, &SolutionWindow::onPrintTriggered, QKeySequence::Print);
  fileMenu->addAction("P&rint Setup...", this, &SolutionWindow::onPrintSetupTriggered);
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
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // Contour (field lines), not Density, is the initially-checked plot
  // mode now -- see MeshSolutionItem::m_mode's default in SolutionView.h.
  // Each handler also refreshes the view's antialiasing against the
  // CURRENT zoom level (SolutionGraphicsView::updateAntialiasingForScale)
  // when its mode becomes active, rather than leaving whatever AA state
  // happened to be set by the last zoom/wheel event -- matters most for
  // Density, the expensive one to rasterize on a large mesh, since it's
  // no longer the default and only gets enabled by deliberate user
  // action now.
  QAction* densityAction = viewMenu->addAction("&Density Plot");
  densityAction->setCheckable(true);
  plotGroup->addAction(densityAction);
  connect(densityAction, &QAction::triggered, this, [this]() {
    if (m_item)
      m_item->setPlotMode(MeshSolutionItem::PlotMode::Density);
    m_view->updateAntialiasingForScale();
    m_view->refreshLegend();
  });
  QAction* contourAction = viewMenu->addAction("&Contour Plot");
  contourAction->setCheckable(true);
  contourAction->setChecked(true);
  plotGroup->addAction(contourAction);
  connect(contourAction, &QAction::triggered, this, [this]() {
    if (m_item)
      m_item->setPlotMode(MeshSolutionItem::PlotMode::Contour);
    m_view->updateAntialiasingForScale();
    m_view->refreshLegend();
  });
  QAction* vectorAction = viewMenu->addAction("&Vector Plot");
  vectorAction->setCheckable(true);
  plotGroup->addAction(vectorAction);
  connect(vectorAction, &QAction::triggered, this, [this]() {
    if (m_item)
      m_item->setPlotMode(MeshSolutionItem::PlotMode::Vector);
    m_view->updateAntialiasingForScale();
    m_view->refreshLegend();
  });
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // per user request for "all the different heatmap possibilities" the
  // classic GUI's Density Plot offers -- see MeshSolutionItem::
  // DensityQuantity's header comment for exactly which ones. Applies
  // regardless of which plot mode is currently active, matching how
  // Smoothing/Show Mesh/Show Points below are all independent toggles
  // rather than plot modes of their own -- only actually visible in
  // Density mode's own rendering, but there's no harm in it being
  // selectable while Contour/Vector is shown too.
  QMenu* densityQtyMenu = viewMenu->addMenu("Density &Quantity");
  auto* densityQtyGroup = new QActionGroup(this);
  densityQtyGroup->setExclusive(true);
  auto addDensityQtyAction = [&](const QString& text, MeshSolutionItem::DensityQuantity q, bool checked) {
    QAction* a = densityQtyMenu->addAction(text);
    a->setCheckable(true);
    a->setChecked(checked);
    densityQtyGroup->addAction(a);
    connect(a, &QAction::triggered, this, [this, q]() {
      if (m_item)
        m_item->setDensityQuantity(q);
      m_view->refreshLegend();
    });
  };
  addDensityQtyAction("|B| (Tesla)", MeshSolutionItem::DensityQuantity::BMag, true);
  addDensityQtyAction("|B_re| (Tesla)", MeshSolutionItem::DensityQuantity::BReMag, false);
  addDensityQtyAction("|B_im| (Tesla)", MeshSolutionItem::DensityQuantity::BImMag, false);
  addDensityQtyAction("log10(|B|)", MeshSolutionItem::DensityQuantity::LogBMag, false);
  addDensityQtyAction("|H| (Amp/m)", MeshSolutionItem::DensityQuantity::HMag, false);
  addDensityQtyAction("|Js+Je| (MA/m^2)", MeshSolutionItem::DensityQuantity::JMag, false);

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
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // per user request for a "colourmap bar on the side, similar to old
  // gui" -- mirrors femm/FemmviewView.cpp's own LegendFlag/d_LegendFlag,
  // default-on like classic FEMM's own default.
  QAction* showLegendAction = viewMenu->addAction("Show &Legend");
  showLegendAction->setCheckable(true);
  showLegendAction->setChecked(true);
  connect(showLegendAction, &QAction::toggled, this, [this](bool on) { m_view->setLegendVisible(on); });
  viewMenu->addSeparator();
  viewMenu->addAction("&Circuit Props...", this, &SolutionWindow::onCircuitPropsTriggered);
  viewMenu->addAction("&BH Curves...", this, &SolutionWindow::onBhCurvesTriggered);
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
    refreshToolbarIcons();
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

  // Matches femm.rc's IDR_FEMMVIEWTYPE toolbar -- every one of these
  // already exists as a menu item above; per direct user request (the
  // Solution Viewer had no toolbar icons at all, text menus only) this
  // gives them toolbar buttons too, reusing the exact same checkable
  // QAction objects the menu items above already created (not copies)
  // where one exists, so the toolbar and the menu never disagree about
  // which tool/plot mode is active.
  QToolBar* toolBar = addToolBar("Operation");
  toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
  toolBar->setIconSize(QSize(20, 20));
  toolBar->addAction(m_pointToolAction);
  m_pointToolAction->setIcon(IconTheme::themedToolIcon(":/icons/point_properties.svg"));
  m_pointToolAction->setToolTip("Point Properties -- click a point on the mesh to see the field value there");
  m_themedActions.push_back({ m_pointToolAction, ":/icons/point_properties.svg" });
  toolBar->addAction(m_contourToolAction);
  m_contourToolAction->setIcon(IconTheme::themedToolIcon(":/icons/contours.svg"));
  m_contourToolAction->setToolTip("Contours -- click points to trace a contour for length/integral calculations");
  m_themedActions.push_back({ m_contourToolAction, ":/icons/contours.svg" });
  toolBar->addAction(m_areaToolAction);
  m_areaToolAction->setIcon(IconTheme::themedToolIcon(":/icons/areas.svg"));
  m_areaToolAction->setToolTip("Areas -- click inside a region to compute its area and average field");
  m_themedActions.push_back({ m_areaToolAction, ":/icons/areas.svg" });
  toolBar->addSeparator();
  addThemedAction(toolBar, ":/icons/plot_xy.svg", "Plot X-Y", "Sample field values along the current contour", &SolutionWindow::onPlotXYTriggered);
  addThemedAction(toolBar, ":/icons/integrate.svg", "Integrate", "Compute the line integral along the current contour", &SolutionWindow::onIntegrateTriggered);
  addThemedAction(toolBar, ":/icons/circuit_props.svg", "Circuit Props", "View total current, voltage drop and flux linkage for a circuit", &SolutionWindow::onCircuitPropsTriggered);
  toolBar->addSeparator();
  toolBar->addAction(showMeshAction);
  showMeshAction->setIcon(IconTheme::themedToolIcon(":/icons/mesh.svg"));
  showMeshAction->setToolTip("Show Mesh -- overlay the finite element mesh");
  m_themedActions.push_back({ showMeshAction, ":/icons/mesh.svg" });
  toolBar->addAction(contourAction);
  contourAction->setIcon(IconTheme::themedToolIcon(":/icons/contour_plot.svg"));
  contourAction->setToolTip("Contour Plot -- draw equipotential (constant A) lines");
  m_themedActions.push_back({ contourAction, ":/icons/contour_plot.svg" });
  toolBar->addAction(densityAction);
  densityAction->setIcon(IconTheme::themedToolIcon(":/icons/density_plot.svg"));
  densityAction->setToolTip("Density Plot -- color-shaded field magnitude");
  m_themedActions.push_back({ densityAction, ":/icons/density_plot.svg" });
  toolBar->addAction(vectorAction);
  vectorAction->setIcon(IconTheme::themedToolIcon(":/icons/vector_plot.svg"));
  vectorAction->setToolTip("Vector Plot -- arrows showing field direction");
  m_themedActions.push_back({ vectorAction, ":/icons/vector_plot.svg" });
  HoverTooltip::installOn(toolBar);

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

  m_spatialIndexBuilt = false; // m_solution just got replaced -- see buildSpatialIndex()'s comment
  m_scene->clear();
  m_contourVisual = nullptr; // clear() above already deleted it
  m_contourPoints.clear();
  m_item = new MeshSolutionItem(&m_solution);
  m_scene->addItem(m_item);
  m_view->fitInView(m_item->boundingRect(), Qt::KeepAspectRatio);
  m_view->updateAntialiasingForScale();
  m_view->setLegendItem(m_item);
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

void SolutionWindow::buildSpatialIndex() const
{
  m_spatialIndex = SpatialIndex();
  m_spatialIndexBuilt = true;
  if (m_solution.elements.isEmpty() || m_solution.nodes.isEmpty())
    return;

  double minX = 0, maxX = 0, minY = 0, maxY = 0;
  bool first = true;
  for (const MeshSolutionNode& n : m_solution.nodes) {
    if (first) {
      minX = maxX = n.x;
      minY = maxY = n.y;
      first = false;
    } else {
      minX = std::min(minX, n.x);
      maxX = std::max(maxX, n.x);
      minY = std::min(minY, n.y);
      maxY = std::max(maxY, n.y);
    }
  }

  // One cell per element on average -- a simple uniform grid is enough
  // here (unlike a quadtree, doesn't adapt to locally-uneven element
  // density, but even a many-times-too-coarse or many-times-too-fine
  // grid still turns an O(elementCount) scan into a small-bucket lookup,
  // and finite-element meshes are rarely so wildly non-uniform that this
  // matters in practice).
  double w = std::max(maxX - minX, 1e-12);
  double h = std::max(maxY - minY, 1e-12);
  double cellSize = std::sqrt((w * h) / std::max(1, (int)m_solution.elements.size()));
  if (!(cellSize > 0))
    cellSize = std::max(w, h);
  int cols = std::max(1, (int)(w / cellSize) + 1);
  int rows = std::max(1, (int)(h / cellSize) + 1);

  m_spatialIndex.minX = minX;
  m_spatialIndex.minY = minY;
  m_spatialIndex.cellSize = cellSize;
  m_spatialIndex.cols = cols;
  m_spatialIndex.rows = rows;
  m_spatialIndex.cells.resize(cols * rows);

  for (int i = 0; i < m_solution.elements.size(); i++) {
    const MeshSolutionElement& e = m_solution.elements[i];
    if (e.p0 < 0 || e.p0 >= m_solution.nodes.size() || e.p1 < 0 || e.p1 >= m_solution.nodes.size() || e.p2 < 0 || e.p2 >= m_solution.nodes.size())
      continue;
    const MeshSolutionNode& n0 = m_solution.nodes[e.p0];
    const MeshSolutionNode& n1 = m_solution.nodes[e.p1];
    const MeshSolutionNode& n2 = m_solution.nodes[e.p2];
    double triMinX = std::min({ n0.x, n1.x, n2.x });
    double triMaxX = std::max({ n0.x, n1.x, n2.x });
    double triMinY = std::min({ n0.y, n1.y, n2.y });
    double triMaxY = std::max({ n0.y, n1.y, n2.y });
    int c0 = std::clamp((int)((triMinX - minX) / cellSize), 0, cols - 1);
    int c1 = std::clamp((int)((triMaxX - minX) / cellSize), 0, cols - 1);
    int r0 = std::clamp((int)((triMinY - minY) / cellSize), 0, rows - 1);
    int r1 = std::clamp((int)((triMaxY - minY) / cellSize), 0, rows - 1);
    for (int r = r0; r <= r1; r++)
      for (int c = c0; c <= c1; c++)
        m_spatialIndex.cells[r * cols + c].push_back(i);
  }
}

int SolutionWindow::findContainingElement(QPointF pt) const
{
  if (!m_spatialIndexBuilt)
    buildSpatialIndex();
  if (m_spatialIndex.cols == 0 || m_spatialIndex.rows == 0)
    return -1;

  int c = std::clamp((int)((pt.x() - m_spatialIndex.minX) / m_spatialIndex.cellSize), 0, m_spatialIndex.cols - 1);
  int r = std::clamp((int)((pt.y() - m_spatialIndex.minY) / m_spatialIndex.cellSize), 0, m_spatialIndex.rows - 1);
  const QVector<int>& bucket = m_spatialIndex.cells[r * m_spatialIndex.cols + c];
  for (int i : bucket) {
    const MeshSolutionElement& e = m_solution.elements[i];
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

void SolutionWindow::onCanvasHovered(QPointF scenePos)
{
  if (!m_item) {
    m_positionLabel->clear();
    return;
  }

  // findContainingElement() is a linear scan over every mesh element (see
  // its own comment) -- fine for a single deliberate click, but this
  // slot fires on every pixel of mouse movement, so throttle to roughly
  // 20 updates/sec. Still feels live to a human; keeps a multi-million-
  // element mesh from doing a full scan hundreds of times a second.
  if (m_hoverThrottle.isValid() && m_hoverThrottle.elapsed() < 50)
    return;
  m_hoverThrottle.restart();

  int elem = findContainingElement(scenePos);
  QString text;
  if (elem < 0) {
    text = QString("x = %1, y = %2").arg(scenePos.x(), 0, 'g', 6).arg(scenePos.y(), 0, 'g', 6);
  } else {
    std::complex<double> A = interpolateA(scenePos, elem);
    const MeshSolutionElement& e = m_solution.elements[elem];
    double bMag = std::hypot(std::hypot(e.B1re, e.B1im), std::hypot(e.B2re, e.B2im));
    text = QString("x = %1, y = %2   |B| = %3 T   A = %4")
               .arg(scenePos.x(), 0, 'g', 6)
               .arg(scenePos.y(), 0, 'g', 6)
               .arg(bMag, 0, 'g', 4)
               .arg(A.real(), 0, 'g', 4);
  }
  m_positionLabel->setText(text);
  m_view->setTooltipText(text);
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
    // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
    // H/J added alongside the existing A/B rows -- same formulas as
    // MeshSolutionItem::elementQuantity's HMag/JMag cases (see that
    // method's comment), just also broken into their re/im components
    // here to match how A/B1/B2 are already shown.
    constexpr double kMuo = 1.2566370614359173e-6;
    double h1re = e.B1re / (e.muX * kMuo), h1im = e.B1im / (e.muX * kMuo);
    double h2re = e.B2re / (e.muY * kMuo), h2im = e.B2im / (e.muY * kMuo);
    double hMag = std::hypot(std::hypot(h1re, h1im), std::hypot(h2re, h2im));
    double jMag = std::hypot(e.jRe, e.jIm);

    QDialog dlg(this);
    dlg.setWindowTitle("Point Properties");
    auto* form = new QFormLayout(&dlg);
    form->addRow("x, y:", new QLabel(QString("%1, %2").arg(scenePos.x(), 0, 'g', 6).arg(scenePos.y(), 0, 'g', 6)));
    form->addRow("A (re, im):", new QLabel(QString("%1, %2").arg(A.real(), 0, 'g', 6).arg(A.imag(), 0, 'g', 6)));
    form->addRow("B1 (re, im):", new QLabel(QString("%1, %2").arg(e.B1re, 0, 'g', 6).arg(e.B1im, 0, 'g', 6)));
    form->addRow("B2 (re, im):", new QLabel(QString("%1, %2").arg(e.B2re, 0, 'g', 6).arg(e.B2im, 0, 'g', 6)));
    form->addRow("|B|:", new QLabel(QString("%1 T").arg(bMag, 0, 'g', 6)));
    form->addRow("H1 (re, im):", new QLabel(QString("%1, %2").arg(h1re, 0, 'g', 6).arg(h1im, 0, 'g', 6)));
    form->addRow("H2 (re, im):", new QLabel(QString("%1, %2").arg(h2re, 0, 'g', 6).arg(h2im, 0, 'g', 6)));
    form->addRow("|H|:", new QLabel(QString("%1 A/m").arg(hMag, 0, 'g', 6)));
    form->addRow("Js+Je (re, im):", new QLabel(QString("%1, %2").arg(e.jRe, 0, 'g', 6).arg(e.jIm, 0, 'g', 6)));
    form->addRow("|Js+Je|:", new QLabel(QString("%1 MA/m^2").arg(jMag, 0, 'g', 6)));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    form->addRow(buttons);
    appendOutput(QString("Point: x=%1, y=%2  A=(%3, %4)  B1=(%5, %6)  B2=(%7, %8)  |B|=%9 T  |H|=%10 A/m  |Js+Je|=%11 MA/m^2")
                      .arg(scenePos.x(), 0, 'g', 6)
                      .arg(scenePos.y(), 0, 'g', 6)
                      .arg(A.real(), 0, 'g', 6)
                      .arg(A.imag(), 0, 'g', 6)
                      .arg(e.B1re, 0, 'g', 6)
                      .arg(e.B1im, 0, 'g', 6)
                      .arg(e.B2re, 0, 'g', 6)
                      .arg(e.B2im, 0, 'g', 6)
                      .arg(bMag, 0, 'g', 6)
                      .arg(hMag, 0, 'g', 6)
                      .arg(jMag, 0, 'g', 6));
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
  //
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // this used to just show an info box telling the user to go find the
  // separate Operation > Contours tool first, then come back here --
  // reported as "the plotting tool does not work" (a fair reading: two
  // disconnected steps in different menus, easy to miss). Per user
  // request ("plot the field values across a trajectory that is
  // drawn"), this now activates the Contour tool directly instead of
  // just pointing at it, so drawing the trajectory and requesting the
  // plot are one continuous flow through this single action -- draw
  // points, then click Plot X-Y again (or Operation > Finish Contour)
  // when done.
  if (m_contourPoints.size() < 2) {
    m_toolMode = SolutionToolMode::Contour;
    if (m_contourToolAction)
      m_contourToolAction->setChecked(true);
    statusBar()->showMessage("Plot X-Y: click points to draw the trajectory, then click Plot X-Y again when done.");
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

void SolutionWindow::onCircuitPropsTriggered()
{
  if (m_currentPath.isEmpty()) {
    QMessageBox::information(this, "Circuit Properties", "No solution loaded.");
    return;
  }
  FemmProblem problem;
  QString error;
  if (!FemmFileIO::readFem(m_currentPath, problem, error)) {
    QMessageBox::warning(this, "Circuit Properties", error);
    return;
  }
  if (problem.circuitProps.isEmpty()) {
    QMessageBox::information(this, "Circuit Properties", "This problem has no circuits defined.");
    return;
  }
  QVector<CircuitAnalysis::BlockCircuitInfo> blockCircuitInfo;
  if (!CircuitAnalysis::readBlockCircuitInfo(m_currentPath, blockCircuitInfo, error)) {
    QMessageBox::warning(this, "Circuit Properties", error);
    return;
  }

  QDialog dlg(this);
  dlg.setWindowTitle("Circuit Properties");
  dlg.resize(420, 320);
  auto* layout = new QVBoxLayout(&dlg);

  auto* form = new QFormLayout;
  auto* nameCombo = new QComboBox(&dlg);
  for (const FemmCircuitProp& c : problem.circuitProps)
    nameCombo->addItem(c.name);
  form->addRow("Circuit Name:", nameCombo);
  layout->addLayout(form);

  auto* resultLabel = new QLabel(&dlg);
  resultLabel->setWordWrap(true);
  resultLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  resultLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(resultLabel, 1);

  auto updateResult = [this, &problem, blockCircuitInfo, resultLabel](int index) {
    CircuitAnalysis::Result r = CircuitAnalysis::compute(problem, m_solution, blockCircuitInfo, index + 1);
    if (!r.ok) {
      resultLabel->setText(QString("Total current = %1 Amps\n\n%2")
                                .arg(problem.circuitProps[index].ampsRe, 0, 'g', 6)
                                .arg(r.error));
      return;
    }
    QString text = QString("Total current = %1 Amps\nVoltage Drop = %2 Volts\nFlux Linkage = %3 Webers\n")
                       .arg(complexToString(r.amps), complexToString(r.voltsDrop), complexToString(r.fluxLinkage));
    text += QString("Flux/Current = %1 Henries\nVoltage/Current = %2 Ohms\n")
                .arg(complexToString(r.fluxLinkage / r.amps), complexToString(r.voltsDrop / r.amps));
    if (problem.frequency == 0) {
      text += QString("Power = %1 Watts").arg(std::real(r.amps * r.voltsDrop), 0, 'g', 6);
    } else {
      text += QString("Real Power = %1 Watts\nReactive Power = %2 VAr\nApparent Power = %3 VA")
                  .arg(std::real(r.voltsDrop * std::conj(r.amps)) / 2.0, 0, 'g', 6)
                  .arg(std::imag(r.voltsDrop * std::conj(r.amps)) / 2.0, 0, 'g', 6)
                  .arg(std::abs(r.voltsDrop) * std::abs(r.amps) / 2.0, 0, 'g', 6);
    }
    resultLabel->setText(text);
  };
  connect(nameCombo, &QComboBox::currentIndexChanged, &dlg, updateResult);
  updateResult(0);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  layout->addWidget(buttons);
  dlg.exec();
}

void SolutionWindow::onBhCurvesTriggered()
{
  // Read-only viewer -- femm.rc's IDR_FEMMVIEWTYPE "BH Curves"
  // (FemmviewView.cpp's OnViewBHcurves), distinct from the geometry
  // editor's own "Edit BH Curve..." (BHCurveDialog opened from
  // MaterialPropDialog): this just re-displays whichever nonlinear
  // materials this *solved* problem used, for reference, not editing.
  if (m_currentPath.isEmpty()) {
    QMessageBox::information(this, "BH Curves", "No solution loaded.");
    return;
  }
  FemmProblem problem;
  QString error;
  if (!FemmFileIO::readFem(m_currentPath, problem, error)) {
    QMessageBox::warning(this, "BH Curves", error);
    return;
  }
  QVector<int> nonlinear;
  for (int i = 0; i < problem.materialProps.size(); i++)
    if (!problem.materialProps[i].bhData.isEmpty())
      nonlinear.push_back(i);
  if (nonlinear.isEmpty()) {
    QMessageBox::information(this, "BH Curves", "No nonlinear materials in this solution.");
    return;
  }

  QDialog dlg(this);
  dlg.setWindowTitle("BH Curves");
  dlg.resize(480, 480);
  auto* layout = new QVBoxLayout(&dlg);

  auto* form = new QFormLayout;
  auto* combo = new QComboBox(&dlg);
  for (int idx : nonlinear)
    combo->addItem(problem.materialProps[idx].name);
  form->addRow("Material:", combo);
  layout->addLayout(form);

  auto* chart = new BHCurveChartWidget(&dlg);
  layout->addWidget(chart, 1);
  auto* logCheck = new QCheckBox("Log-log scale", &dlg);
  connect(logCheck, &QCheckBox::toggled, chart, &BHCurveChartWidget::setLogScale);
  layout->addWidget(logCheck);

  connect(combo, &QComboBox::currentIndexChanged, &dlg, [&problem, nonlinear, chart](int i) {
    chart->setPoints(problem.materialProps[nonlinear[i]].bhData);
  });
  chart->setPoints(problem.materialProps[nonlinear[0]].bhData);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  layout->addWidget(buttons);
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

void SolutionWindow::onPrintTriggered()
{
  if (!m_printer)
    m_printer = new QPrinter(QPrinter::HighResolution);
  QPrintDialog dlg(m_printer, this);
  if (dlg.exec() != QDialog::Accepted)
    return;
  QPainter painter(m_printer);
  m_view->render(&painter);
}

void SolutionWindow::onPrintPreviewTriggered()
{
  if (!m_printer)
    m_printer = new QPrinter(QPrinter::HighResolution);
  QPrintPreviewDialog dlg(m_printer, this);
  connect(&dlg, &QPrintPreviewDialog::paintRequested, this, [this](QPrinter* p) {
    QPainter painter(p);
    m_view->render(&painter);
  });
  dlg.exec();
}

void SolutionWindow::onPrintSetupTriggered()
{
  if (!m_printer)
    m_printer = new QPrinter(QPrinter::HighResolution);
  QPageSetupDialog dlg(m_printer, this);
  dlg.exec();
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

QAction* SolutionWindow::addThemedAction(QToolBar* bar, const QString& iconPath, const QString& text, const QString& tooltip, void (SolutionWindow::*slot)())
{
  QAction* action = bar->addAction(IconTheme::themedToolIcon(iconPath), text, this, slot);
  action->setToolTip(tooltip);
  m_themedActions.push_back({ action, iconPath });
  return action;
}

void SolutionWindow::refreshToolbarIcons()
{
  for (const auto& entry : m_themedActions)
    entry.first->setIcon(IconTheme::themedToolIcon(entry.second));
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
