#define _USE_MATH_DEFINES
#include "SolutionView.h"

#include "AnsFileIO.h"
#include "AppPreferences.h"
#include "AppTheme.h"
#include "BHCurveDialog.h"
#include "CircuitAnalysis.h"
#include "ContourPlotOptionsDialog.h"
#include "DensityPlotOptionsDialog.h"
#include "AnsxFileIO.h"
#include "FemmFileIO.h"
#include "FemmProblem.h"
#include "GuiSwitch.h"
#include "HoverTooltip.h"
#include "IconTheme.h"
#include "MainWindow.h"
#include "PlotXYChartWidget.h"
#include "PreferencesDialog.h"

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
#include <QDoubleValidator>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
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
#include <QPushButton>
#include <QResizeEvent>
#include <QRubberBand>
#include <QScrollBar>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kNumBands = 20;
// Fixed on-screen radius (device pixels) for Show Points' mesh-node dots --
// see paintMeshOverlay's use of this for why it needs to be divided by the
// painter's current scale rather than used as a scene-space radius directly.
constexpr double kMeshPointScreenRadius = 2.5;

// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-22: was
// femm/StdAfx.h's literal dColor00..dColor19 table (magenta, lowest, up
// through cyan, highest -- ported earlier this session specifically
// because it never passes through blue, avoiding a collision with
// AppTheme::segmentColor/arcColor -- see the git history for that
// investigation). Replaced again per direct user correction ("the higher
// values in the density plot should be hotter not colder than the lower
// values... follow the colors in the density plots as in the images I
// sent") plus two reference mockup images showing a classic blue(low) ->
// cyan -> green -> yellow -> orange -> red(high) "jet" spectrum in BOTH
// the light- and dark-theme mockups (sampled 20 real pixel colors
// directly from the reference image's own legend bar, not hand-guessed).
// This reintroduces blue at the low end, which is exactly what the
// PREVIOUS change was ported to avoid -- AppTheme::segmentColor/arcColor
// were changed to the new muted palette's accents in the meantime
// (#007ACC/#4EC9B0, themselves blue/teal), so this is a real, live
// collision risk again, not a hypothetical one. Deliberately not solved
// preemptively here (e.g. with an outline/halo around the overlay lines)
// since that reintroduces the nonzero-cosmetic-pen precision problem the
// "field lines are magnified" fix specifically avoided -- verify the
// overlay is still legible against this on a real model before assuming
// it's fine, and address the overlay colors specifically if not, rather
// than picking a "safer" density spectrum that stops matching the user's
// reference images.
constexpr QRgb kColorMap[kNumBands] = {
  qRgb(18, 31, 169), qRgb(21, 48, 191), qRgb(22, 87, 208), qRgb(24, 106, 214),
  qRgb(30, 128, 222), qRgb(46, 175, 224), qRgb(57, 200, 222), qRgb(83, 218, 181),
  qRgb(97, 222, 162), qRgb(104, 224, 148), qRgb(172, 231, 60), qRgb(198, 233, 40),
  qRgb(250, 221, 16), qRgb(253, 199, 12), qRgb(253, 178, 9), qRgb(247, 118, 11),
  qRgb(243, 95, 16), qRgb(223, 38, 35), qRgb(211, 26, 40), qRgb(173, 18, 37),
};
// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-22: was
// femm/StdAfx.h's literal dGrey00..dGrey19 (55->245 in steps of 10) --
// confirmed via a direct render of a real model (the transformer test
// file) that this reads as almost no visible gradient at all, even
// though the underlying band assignment is correct: 20 distinct grey
// values ARE used, but a 10-unit RGB step between adjacent bands is well
// under what's reliably perceptible, especially compared to the color
// palette's large hue jumps between bands. Widened to the full 0->255
// range (19-unit steps, ~90% more contrast per band) -- a deliberate,
// user-requested improvement over classic's own cramped range, not a
// faithful-port choice like kColorMap above. 0 (pure black) was checked
// against this app's dark-theme background (~30,30,30, AppTheme::
// background()) and stays visibly distinguishable from it, so this
// doesn't reintroduce the same "lowest band blends into the background"
// problem that motivated kColorMap's own port.
constexpr QRgb kGreyMap[kNumBands] = {
  qRgb(0, 0, 0), qRgb(13, 13, 13), qRgb(27, 27, 27), qRgb(40, 40, 40),
  qRgb(54, 54, 54), qRgb(67, 67, 67), qRgb(81, 81, 81), qRgb(94, 94, 94),
  qRgb(108, 108, 108), qRgb(121, 121, 121), qRgb(134, 134, 134), qRgb(148, 148, 148),
  qRgb(161, 161, 161), qRgb(175, 175, 175), qRgb(188, 188, 188), qRgb(202, 202, 202),
  qRgb(215, 215, 215), qRgb(229, 229, 229), qRgb(242, 242, 242), qRgb(255, 255, 255),
};

QColor bandColor(int band, bool grayscale)
{
  band = std::clamp(band, 0, kNumBands - 1);
  return QColor::fromRgb(grayscale ? kGreyMap[band] : kColorMap[band]);
}

// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-22: per
// user request ("in the old gui I think the heatmaps looks smoother, can
// you have an option for that and check how that is implemented") --
// femm/FemmviewView.cpp's PlotFluxDensity does NOT use GDI's GradientFill
// (confirmed via an exhaustive grep of the whole femm/ tree for
// GradientFill/Gouraud/TRIVERTEX -- zero matches). It marching-triangle
// slices each element into up to kNumBands flat-colored sub-polygons
// hugging the exact iso-value lines of its 3 corners' values, instead of
// filling the whole triangle with one flat color -- what femmqt's
// m_smooth previously did (band on the AVERAGE of the 3 corners) softened
// element-to-element steps but not the coarse, one-color-per-whole-
// element blockiness within a single element that's straddling several
// bands, which is what actually reads as "smooth" in the classic GUI at
// typical zoom levels. appendEdgeCrossings walks one triangle edge from
// (pA,vA) to (pB,vB) in "band space" (vA/vB already rescaled to
// 0..kNumBands) and appends every INTEGER level strictly between them, in
// the order encountered, as (position, level) pairs -- linear
// interpolation of POSITION at the same parameter t as the value crossing.
void appendEdgeCrossings(QPointF pA, double vA, QPointF pB, double vB, QVector<QPair<QPointF, double>>& out)
{
  if (vA == vB)
    return;
  int lvlLo = (int)std::floor(std::min(vA, vB)) + 1;
  int lvlHi = (int)std::ceil(std::max(vA, vB)) - 1;
  if (vA < vB) {
    for (int L = lvlLo; L <= lvlHi; L++)
      out.push_back({ pA + (L - vA) / (vB - vA) * (pB - pA), (double)L });
  } else {
    for (int L = lvlHi; L >= lvlLo; L--)
      out.push_back({ pA + (L - vA) / (vB - vA) * (pB - pA), (double)L });
  }
}

// Slices one triangle (corners p0/p1/p2, each with a value already
// rescaled to band-space 0..kNumBands) into flat-colored sub-polygons,
// adding each to the matching bandPaths[] entry -- femm/FemmviewView.cpp's
// exact algorithm: walk the 3 edges collecting corners + every integer
// crossing into a perimeter-ordered polygon, then repeatedly ear-clip the
// LOWEST-value remaining vertex together with its two cyclic neighbors
// into one flat-colored sub-triangle (banded on their 3-value mean) until
// only 2 vertices remain. Produces up to ~kNumBands thin slivers per
// element, each following the iso-value lines exactly, rather than one
// flat color for the whole triangle.
void sliceTriangleIntoBands(QPointF p0, double v0, QPointF p1, double v1, QPointF p2, double v2, QPainterPath bandPaths[])
{
  QVector<QPair<QPointF, double>> perim;
  perim.reserve(9);
  QPointF pts[3] = { p0, p1, p2 };
  double vals[3] = { v0, v1, v2 };
  for (int i = 0; i < 3; i++) {
    perim.push_back({ pts[i], vals[i] });
    appendEdgeCrossings(pts[i], vals[i], pts[(i + 1) % 3], vals[(i + 1) % 3], perim);
  }
  while (perim.size() > 2) {
    int n = perim.size();
    int idx = 0;
    for (int i = 1; i < n; i++)
      if (perim[i].second < perim[idx].second)
        idx = i;
    int prev = (idx - 1 + n) % n;
    int next = (idx + 1) % n;
    double meanV = (perim[prev].second + perim[idx].second + perim[next].second) / 3.0;
    int band = std::clamp((int)std::floor(meanV), 0, kNumBands - 1);
    QPolygonF tri;
    tri << perim[prev].first << perim[idx].first << perim[next].first;
    bandPaths[band].addPolygon(tri);
    perim.remove(idx);
  }
}

// Same math as GeometryScene.cpp's own arcGeometry() -- an independent
// copy rather than a shared header, matching this codebase's established
// precedent (see FemxFileIO.cpp/AnsxFileIO.cpp's identical top-of-file
// note) for small, self-contained helpers like this one.
bool arcGeometry(double x0, double y0, double x1, double y1, double arcLengthDeg,
    double& cx, double& cy, double& R, double& startAngleDeg)
{
  double dx = x1 - x0, dy = y1 - y0;
  double d = std::hypot(dx, dy);
  if (d <= 0)
    return false;
  double tta = arcLengthDeg * M_PI / 180.0;
  double s = std::sin(tta / 2.0);
  if (std::abs(s) < 1e-12)
    return false;
  R = d / (2.0 * s);
  double tx = dx / d, ty = dy / d;
  double h = std::sqrt(std::max(0.0, R * R - d * d / 4.0));
  cx = x0 + (d / 2.0 * tx - h * ty);
  cy = y0 + (d / 2.0 * ty + h * tx);
  // Sign-flipped to match Qt's QPainterPath::arcTo angle convention -- see
  // the matching fix/comment on GeometryScene.cpp's copy of this function.
  startAngleDeg = std::atan2(-(y0 - cy), x0 - cx) * 180.0 / M_PI;
  return true;
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

// Own copy of MainWindow.cpp's identically-named/implemented helper (needs
// to live up here, ahead of openAnsFile below, rather than alongside this
// file's other small file-local helpers further down) -- see that
// function's comment for the reasoning: per direct user request, ~20 grid
// squares across the model's larger dimension after the initial fit-to-
// view, snapped to a whole-number 1/2/5-times-a-power-of-ten step.
double niceIntegerGridSize(const QRectF& bounds)
{
  double extent = std::max(bounds.width(), bounds.height());
  if (extent <= 0)
    return 1.0;
  double target = extent / 20.0;
  if (target < 1.0)
    return 1.0;
  double magnitude = std::pow(10.0, std::floor(std::log10(target)));
  double normalized = target / magnitude;
  double nice;
  if (normalized < 1.5)
    nice = 1;
  else if (normalized < 3.5)
    nice = 2;
  else if (normalized < 7.5)
    nice = 5;
  else
    nice = 10;
  return nice * magnitude;
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

// Modified by Claude (Anthropic), noreply@anthropic.com: shared by
// MeshSolutionItem::elementQuantity, SolutionGraphicsView::onCanvasHovered,
// and SolutionWindow's Point Properties tool (onCanvasClicked) -- these
// used to each duplicate the same H = B/(mu*mu0) formula (the hover/
// click sites even said so explicitly, "duplicated rather than shared").
// Per direct user report ("the density plot for H is wrong ... compare
// with the old gui") -- muX/muY are a linear-material-only placeholder
// (see MeshSolutionElement's own comment); for a nonlinear (BH-curve)
// material this was off by orders of magnitude (confirmed live: ~5-6
// orders of magnitude at a low-flux point on a nanocrystalline-core
// test model, since the placeholder implies mu_r=1 while the real
// material's initial permeability there was ~8.2 million). Matches
// CFemmviewDoc::GetH's isotropic (LamType==0) nonlinear case: a scalar
// |H| comes off the BH curve at |B|, then H is kept parallel to B
// (H1=B1*|H|/|B|, H2=B2*|H|/|B|) -- see BHCurve.h for the curve-fitting
// math this ports from femm/Problem.cpp's CMaterialProp::GetSlopes/GetH.
// Falls back to the original muX/muY formula for linear materials, or
// for the nonlinear cases BHCurve.h's own comment documents as not
// (yet) ported (AC/harmonic, laminated, permanent-magnet).
void computeElementH(const MeshSolutionElement& e, const MeshSolution& solution,
    double& h1re, double& h1im, double& h2re, double& h2im)
{
  if (e.bhMaterialIndex >= 0 && e.bhMaterialIndex < solution.nonlinearMaterials.size()) {
    const BHCurve::Curve& curve = solution.nonlinearMaterials[e.bhMaterialIndex];
    double bMag = std::hypot(e.B1re, e.B2re); // DC only -- see BHCurve.h's AC/harmonic scope note
    double hMag = BHCurve::interpolateH(curve, bMag);
    // Degenerate near-zero-B case, matching CMaterialProp::GetMu's own
    // "biron < 1e-8" branch: avoid a 0/0 by using the curve's initial
    // slope (dH/dB at the origin) directly rather than hMag/bMag.
    double hPerB = (bMag < 1e-8) ? (curve.slope.isEmpty() ? 0.0 : curve.slope[0]) : (hMag / bMag);
    h1re = e.B1re * hPerB;
    h2re = e.B2re * hPerB;
    h1im = h2im = 0.0; // real-valued (DC) curve only, see above
    return;
  }
  constexpr double kMuo = 1.2566370614359173e-6;
  h1re = e.B1re / (e.muX * kMuo);
  h1im = e.B1im / (e.muX * kMuo);
  h2re = e.B2re / (e.muY * kMuo);
  h2im = e.B2im / (e.muY * kMuo);
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

  // Modified by Claude (Anthropic), noreply@anthropic.com: matches femm/
  // FemmviewDoc.cpp's own "catch the special case where _every_ element
  // seems to be in an external region" fallback -- if excluding
  // MeshSolutionElement::isExternal elements from the range below would
  // leave nothing to search (a model that's somehow all ABC shell/
  // Exterior Region, or just none of either), don't exclude anything
  // rather than left qd.vMin/vMax at their default-constructed 0/0.
  bool anyNonExternal = false;
  for (const MeshSolutionElement& e : solution->elements) {
    if (!e.isExternal) {
      anyNonExternal = true;
      break;
    }
  }

  // Precompute each node's average value (across every element touching
  // it -- see QuantityData's header comment for why) plus the min/max
  // range, for every DensityQuantity at once. Done once here rather than
  // per-paint or per-quantity-switch, since paint() can run many times
  // (every pan/zoom) but the mesh itself never changes -- the whole
  // point of this precompute pass, same as the original |B|-only version.
  for (int qi = 0; qi < kDensityQuantityCount; qi++) {
    auto q = static_cast<DensityQuantity>(qi);
    QuantityData& qd = m_quantityData[qi];
    qd.nodeAvg.fill(0.0, solution->nodes.size());
    QVector<int> touchCount(solution->nodes.size(), 0);
    bool first = true;
    // Size-weighted candidate score for vMax -- see MeshSolutionElement::
    // rsqr's comment. -1 so even a genuine 0-valued mesh still picks a
    // candidate on the first eligible element (weight is always >= 0).
    double bestWeight = -1.0;
    for (const MeshSolutionElement& e : solution->elements) {
      double v = elementQuantity(e, q);
      // Modified by Claude (Anthropic), noreply@anthropic.com: per direct
      // user report ("compare the flux density in the old and new gui
      // from .ansx they do not look similar") -- root-caused to exactly
      // matching femm/FemmviewDoc.cpp's own isExt[] exclusion (see
      // MeshSolutionElement::isExternal's comment): elements inside
      // mi_makeABC's Kelvin-transform shells can compute a legitimately
      // huge B (confirmed matching classic's own GetElementB/mo_getb
      // exactly at the same point -- not a wrong-formula bug), which
      // isn't a real physical flux density and shouldn't stretch the
      // whole density plot's color range the way it was. Still
      // contributes to nodeAvg below (still drawn, just not searched for
      // the range), matching PlotFluxDensity's own behavior.
      if (!(anyNonExternal && e.isExternal)) {
        if (first) {
          qd.vMin = v;
          first = false;
        } else {
          qd.vMin = std::min(qd.vMin, v);
        }
        // Modified by Claude (Anthropic), noreply@anthropic.com: per the
        // same user report -- excluding isExternal alone wasn't enough. A
        // handful of small, awkwardly-shaped (but perfectly legitimate,
        // non-external) elements right at this app's own geometry
        // corners -- e.g. where a Draw Rectangle/Circle wedge's straight
        // edge meets its arc -- computed a real-per-formula but locally
        // spurious high B that a plain max() let dominate vMax, badly
        // skewing the whole plot's color range toward one or two pixels'
        // worth of area (confirmed live: elementCount for the top band
        // was ~4x its neighbors', all clustered at a handful of tiny
        // rects). femm/FemmviewDoc.cpp's own B_High search has exactly
        // this same protection built in (a1 = sqrt(rsqr)*b*b candidate
        // weighting, "discounts really small elements with really high
        // flux density, which sometimes happens in corners") -- adapted
        // here to femmqt's one-value-per-element model (classic's version
        // uses nodal, not per-element, B).
        double weight = std::sqrt(e.rsqr) * v * v;
        if (weight > bestWeight) {
          bestWeight = weight;
          qd.vMax = v;
        }
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
  //
  // Modified by Claude (Anthropic), noreply@anthropic.com: per direct
  // user report ("the density plot for H is wrong ... compare with the
  // old gui") -- e.bhMaterialIndex >= 0 means e's material is nonlinear
  // (see BHCurve.h and that field's own comment); the muX/muY-based
  // formula below implicitly assumed mu_r=1 for those elements (muX/muY
  // are only ever a linear-material placeholder), off from the correct,
  // B-dependent permeability by orders of magnitude for a real
  // ferromagnetic core (confirmed live: ~5-6 orders of magnitude at a
  // low-flux point on a nanocrystalline-core test model). Matches
  // CFemmviewDoc::GetH's isotropic (LamType==0) case: a scalar |H| comes
  // off the BH curve at |B|, then H is kept parallel to B (H1=B1*|H|/
  // |B|, H2=B2*|H|/|B|) -- physically correct for an isotropic nonlinear
  // material, same assumption classic makes.
  case DensityQuantity::HMag:
  case DensityQuantity::HReMag:
  case DensityQuantity::HImMag: {
    double h1re, h1im, h2re, h2im;
    computeElementH(e, *m_solution, h1re, h1im, h2re, h2im);
    if (q == DensityQuantity::HReMag)
      return std::hypot(h1re, h2re);
    if (q == DensityQuantity::HImMag)
      return std::hypot(h1im, h2im);
    return std::hypot(std::hypot(h1re, h1im), std::hypot(h2re, h2im));
  }
  // jRe/jIm are precomputed once (AnsFileIO::readAns) -- see
  // MeshSolutionElement's comment for why (needs nodal A, not available
  // from a single element at paint time). Re(J)/Im(J) match femm/
  // FemmviewView.cpp's PlotFluxDensity cases 8/9 exactly (fabs of each
  // component alone, not a magnitude of a 2-vector like B/H -- J is a
  // single complex scalar for a 2-D problem, not a planar vector).
  case DensityQuantity::JMag: return std::hypot(e.jRe, e.jIm);
  case DensityQuantity::JReMag: return std::fabs(e.jRe);
  case DensityQuantity::JImMag: return std::fabs(e.jIm);
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
  // Reset the legend's displayed range immediately -- otherwise it would
  // briefly show the PREVIOUS quantity's cached range (wrong units/scale
  // entirely) until the next paintDensity() call overwrites it. Prefers
  // this quantity's own custom range if one was set (setCustomRange),
  // matching classic's per-quantity PlotBounds persistence; falls back to
  // the whole-mesh auto range otherwise.
  int idx = static_cast<int>(q);
  if (m_useCustomRange[idx]) {
    m_lastDensityLo = m_customLo[idx];
    m_lastDensityHi = m_customHi[idx];
  } else {
    m_lastDensityLo = m_quantityData[idx].vMin;
    m_lastDensityHi = m_quantityData[idx].vMax;
  }
  update();
}

void MeshSolutionItem::setGrayscale(bool on)
{
  m_grayscale = on;
  update();
}

bool MeshSolutionItem::hasCustomRange(DensityQuantity q) const
{
  return m_useCustomRange[static_cast<int>(q)];
}

void MeshSolutionItem::customRange(DensityQuantity q, double& lo, double& hi) const
{
  int idx = static_cast<int>(q);
  lo = m_customLo[idx];
  hi = m_customHi[idx];
}

void MeshSolutionItem::setCustomRange(DensityQuantity q, double lo, double hi)
{
  int idx = static_cast<int>(q);
  m_useCustomRange[idx] = true;
  m_customLo[idx] = lo;
  m_customHi[idx] = hi;
  if (q == m_densityQuantity) {
    m_lastDensityLo = lo;
    m_lastDensityHi = hi;
  }
  update();
}

void MeshSolutionItem::clearCustomRange(DensityQuantity q)
{
  int idx = static_cast<int>(q);
  m_useCustomRange[idx] = false;
  if (q == m_densityQuantity) {
    m_lastDensityLo = m_quantityData[idx].vMin;
    m_lastDensityHi = m_quantityData[idx].vMax;
  }
  update();
}

void MeshSolutionItem::densityQuantityAutoRange(DensityQuantity q, double& lo, double& hi) const
{
  const QuantityData& qd = m_quantityData[static_cast<int>(q)];
  lo = qd.vMin;
  hi = qd.vMax;
}

int MeshSolutionItem::legendBandCount()
{
  return kNumBands;
}

QColor MeshSolutionItem::legendBandColor(int band) const
{
  return bandColor(band, m_grayscale);
}

void MeshSolutionItem::legendRange(double& lo, double& hi) const
{
  lo = m_lastDensityLo;
  hi = m_lastDensityHi;
}

QString MeshSolutionItem::legendTitle(DensityQuantity q) const
{
  switch (q) {
  case DensityQuantity::BMag: return "|B|, Tesla";
  case DensityQuantity::BReMag: return "|B_re|, Tesla";
  case DensityQuantity::BImMag: return "|B_im|, Tesla";
  case DensityQuantity::HMag: return "|H|, Amp/m";
  case DensityQuantity::HReMag: return "|H_re|, Amp/m";
  case DensityQuantity::HImMag: return "|H_im|, Amp/m";
  case DensityQuantity::JMag: return "|Js+Je|, MA/m^2";
  case DensityQuantity::JReMag: return "|Js+Je|_re, MA/m^2";
  case DensityQuantity::JImMag: return "|Js+Je|_im, MA/m^2";
  case DensityQuantity::LogBMag: return "log10(|B|), log(Tesla)";
  }
  return QString();
}

void MeshSolutionItem::setSmoothing(bool smooth)
{
  m_smooth = smooth;
  update();
}

void MeshSolutionItem::setNumContours(int n)
{
  m_numContours = n;
  update();
}

void MeshSolutionItem::contourRange(double& lo, double& hi) const
{
  if (m_useCustomContourRange) {
    lo = m_customContourLo;
    hi = m_customContourHi;
  } else {
    lo = m_aMin;
    hi = m_aMax;
  }
}

void MeshSolutionItem::setContourRange(double lo, double hi)
{
  m_useCustomContourRange = true;
  m_customContourLo = lo;
  m_customContourHi = hi;
  update();
}

void MeshSolutionItem::clearContourRange()
{
  m_useCustomContourRange = false;
  update();
}

void MeshSolutionItem::setShowImagContour(bool show)
{
  m_showImagContour = show;
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

void MeshSolutionItem::setShowBlockNames(bool show)
{
  m_showBlockNames = show;
  update();
}

void MeshSolutionItem::toggleBlockLabelSelected(int lbl)
{
  if (m_selectedBlockLabels.contains(lbl))
    m_selectedBlockLabels.remove(lbl);
  else
    m_selectedBlockLabels.insert(lbl);
  update();
}

void MeshSolutionItem::clearBlockLabelSelection()
{
  if (m_selectedBlockLabels.isEmpty())
    return;
  m_selectedBlockLabels.clear();
  update();
}

void MeshSolutionItem::setProblemGeometry(const FemmProblem* problem)
{
  m_problemGeometry = problem;
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
  }
  if (!m_selectedBlockLabels.isEmpty())
    paintSelectedBlocks(painter, exposedRect);
  if (m_showMesh || m_showPoints)
    paintMeshOverlay(painter, exposedRect);
  if (m_problemGeometry)
    paintProblemGeometry(painter, exposedRect);
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
  int qIdx = static_cast<int>(m_densityQuantity);
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-22: a
  // custom range (Density Plot Options -> Custom Range) is a fixed,
  // user-chosen override -- skip the zoom-adaptive local-range scan
  // entirely rather than compute it and then discard it, both because
  // it's pointless work and because it's the more expensive of
  // paintDensity's two per-visible-element passes on a huge mesh.
  if (m_useCustomRange[qIdx]) {
    lo = m_customLo[qIdx];
    hi = m_customHi[qIdx];
  } else {
    // Modified by Claude (Anthropic), noreply@anthropic.com: see the
    // constructor's identical isExternal exclusion for why -- mirrored
    // here for this same range calculation's zoom-adaptive (visible-
    // elements-only) variant. loIncl/hiIncl (every visible element,
    // regardless of isExternal) is the fallback for the degenerate case
    // where every currently-visible element happens to be external (e.g.
    // zoomed into just the ABC shell region) -- matches the constructor's
    // own "don't exclude anything if that would exclude everything".
    bool first = true;
    bool firstIncl = true;
    double loIncl = 0, hiIncl = 0;
    // Size-weighted vMax candidates -- see the constructor's identical
    // heuristic (MeshSolutionElement::rsqr's comment) for why hi/hiIncl
    // aren't just plain max()s here either.
    double bestWeight = -1.0, bestWeightIncl = -1.0;
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
      double weight = std::sqrt(e.rsqr) * v * v;
      if (firstIncl) {
        loIncl = v;
        firstIncl = false;
      } else {
        loIncl = std::min(loIncl, v);
      }
      if (weight > bestWeightIncl) {
        bestWeightIncl = weight;
        hiIncl = v;
      }
      if (!e.isExternal) {
        if (first) {
          lo = v;
          first = false;
        } else {
          lo = std::min(lo, v);
        }
        if (weight > bestWeight) {
          bestWeight = weight;
          hi = v;
        }
      }
    }
    if (first) {
      lo = loIncl;
      hi = hiIncl;
    }
    if (firstIncl || hi <= lo) {
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

    if (m_smooth && span > 0) {
      // Marching-triangle band slicing on the 3 corners' individual
      // node-averaged values -- see sliceTriangleIntoBands' comment. This
      // is what actually produces the smooth-gradient look (continuity
      // across shared nodes via nodeAvg, fine intra-element banding via
      // the slicing itself), not just averaging the 3 corners into one
      // flat color the way this branch used to.
      double bv0 = (qd.nodeAvg[e.p0] - bMin) / span * kNumBands;
      double bv1 = (qd.nodeAvg[e.p1] - bMin) / span * kNumBands;
      double bv2 = (qd.nodeAvg[e.p2] - bMin) / span * kNumBands;
      sliceTriangleIntoBands(QPointF(n0.x, n0.y), bv0, QPointF(n1.x, n1.y), bv1, QPointF(n2.x, n2.y), bv2, bandPaths);
    } else {
      double bMag = m_smooth ? (qd.nodeAvg[e.p0] + qd.nodeAvg[e.p1] + qd.nodeAvg[e.p2]) / 3.0 : elementQuantity(e, m_densityQuantity);
      int band = (span > 0) ? (int)((bMag - bMin) / span * kNumBands) : 0;
      band = std::clamp(band, 0, kNumBands - 1);

      QPolygonF tri;
      tri << QPointF(n0.x, n0.y) << QPointF(n1.x, n1.y) << QPointF(n2.x, n2.y);
      bandPaths[band].addPolygon(tri);
    }
  }

  painter->setPen(Qt::NoPen);
  for (int b = 0; b < kNumBands; b++) {
    if (bandPaths[b].isEmpty())
      continue;
    painter->setBrush(bandColor(b, m_grayscale));
    painter->drawPath(bandPaths[b]);
  }
}

void MeshSolutionItem::paintSelectedBlocks(QPainter* painter, const QRectF& exposedRect)
{
  // Matches femm/FemmviewView.cpp's PlotSelectedElm: a solid, opaque fill
  // (not a translucent overlay) over every element whose block label is
  // currently selected, completely replacing whatever Density/Contour
  // color was there -- same visual effect as classic's own solid
  // RegionColor brush.
  QPainterPath selPath;
  for (int ei : elementsOverlapping(exposedRect)) {
    const MeshSolutionElement& e = m_solution->elements[ei];
    if (!m_selectedBlockLabels.contains(e.lbl))
      continue;
    if (e.p0 < 0 || e.p0 >= m_solution->nodes.size() || e.p1 < 0 || e.p1 >= m_solution->nodes.size() || e.p2 < 0 || e.p2 >= m_solution->nodes.size())
      continue;
    const MeshSolutionNode& n0 = m_solution->nodes[e.p0];
    const MeshSolutionNode& n1 = m_solution->nodes[e.p1];
    const MeshSolutionNode& n2 = m_solution->nodes[e.p2];
    QPolygonF tri;
    tri << QPointF(n0.x, n0.y) << QPointF(n1.x, n1.y) << QPointF(n2.x, n2.y);
    selPath.addPolygon(tri);
  }
  if (selPath.isEmpty())
    return;
  painter->setPen(Qt::NoPen);
  painter->setBrush(AppTheme::regionSelectionColor());
  painter->drawPath(selPath);
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
  // Modified by Claude (Anthropic), noreply@anthropic.com: Number of
  // Contours and the Lower/Upper Bound range are now user-configurable
  // (ContourPlotOptionsDialog), matching femm/FemmviewView.cpp's OnCplot/
  // IDD_CPLOTDLG(2) -- was a hardcoded kNumLevels=20 always spanning the
  // whole mesh's Are extremes. "Restore Default Range" in that dialog
  // reverts to exactly that whole-mesh range (m_aMin/m_aMax), matching
  // classic's own Reset behavior.
  double aMin, aMax;
  if (m_useCustomContourRange) {
    aMin = m_customContourLo;
    aMax = m_customContourHi;
  } else {
    aMin = m_aMin;
    aMax = m_aMax;
  }
  double span = aMax - aMin;
  if (span <= 0 || m_numContours <= 0)
    return;

  QPen pen(AppTheme::meshPointColor());
  pen.setCosmetic(true);
  // Width 0, not the QPen(color) ctor's default of 1 -- see
  // GeometryScene.cpp's addNodeItem for the full explanation. This is the
  // pen behind the reported "field lines are magnified" bug on a real,
  // detailed model: a cosmetic pen with a nonzero logical width goes
  // through general stroke tessellation (computing an offset via the
  // transform's inverse scale), which loses precision at the extreme
  // accumulated zoom fine wire-level detail can require; width exactly 0
  // uses Qt's simpler, more robust hairline path instead.
  pen.setWidth(0);

  QPainterPath rePath = contourPath(exposedRect, aMin, span, m_numContours, /*useImag=*/false);
  painter->setPen(pen);
  painter->drawPath(rePath);

  // AC solutions only -- see setShowImagContour's declaration.
  if (m_showImagContour) {
    QPainterPath imPath = contourPath(exposedRect, aMin, span, m_numContours, /*useImag=*/true);
    QPen imagPen(AppTheme::boundaryEdgeColor());
    imagPen.setCosmetic(true);
    imagPen.setWidth(0);
    painter->setPen(imagPen);
    painter->drawPath(imPath);
  }
}

QPainterPath MeshSolutionItem::contourPath(const QRectF& exposedRect, double aMin, double span, int numLevels, bool useImag) const
{
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

    double va[3] = { useImag ? n0.Aim : n0.Are, useImag ? n1.Aim : n1.Are, useImag ? n2.Aim : n2.Are };
    QPointF pa[3] = { QPointF(n0.x, n0.y), QPointF(n1.x, n1.y), QPointF(n2.x, n2.y) };

    for (int lvl = 1; lvl < numLevels; lvl++) {
      double level = aMin + span * lvl / numLevels;
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
  return path;
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
    pen.setWidth(0); // see addNodeItem's (GeometryScene.cpp) comment on width 0 vs the QPen(color) ctor's default of 1
    painter->setPen(pen);
    painter->drawPath(path);
  }

  if (m_showPoints) {
    // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
    // was a fixed fraction of the model's diagonal (diag * 0.0015) --
    // drawEllipse's radius is in SCENE units, so that made the dots grow
    // right along with the model on every zoom-in, unlike the mesh edges
    // just above (a cosmetic QPen, which Qt already keeps at a constant
    // DEVICE-pixel width regardless of the view transform). QPen's
    // cosmetic flag only affects stroked lines, not a filled shape's own
    // geometry, so a filled circle needs the same effect done by hand:
    // divide a fixed on-screen pixel radius by the painter's current
    // world-transform scale, so the SCENE-space radius shrinks exactly
    // enough to keep the drawn size constant on screen. m11() alone
    // (not a full singular-value decomposition) is enough here since
    // this app only ever applies uniform scale() zooming, never skew or
    // non-uniform scale.
    double screenScale = painter->worldTransform().m11();
    double r = kMeshPointScreenRadius / std::max(screenScale, 1e-9);
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

// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21: the
// original problem geometry (nodes/segments/arcs, as opposed to the
// solved FE mesh paintMeshOverlay draws) -- see this method's declaration
// in SolutionView.h for why femmqt needed this at all. Segments/arcs
// always drawn (matching femm/FemmviewView.cpp's own unconditional
// "Draw lines linking nodes" -- classic FEMM has no toggle for hiding
// them, only per-entity Hidden flags, which this respects); nodes as
// small fixed-screen-size squares, matching classic's own 4x4-pixel box
// marker (MyMoveTo/LineTo around xs+-2,ys+-2) -- same screen-space-radius
// technique as paintMeshOverlay's Show Points dots just above, since a
// scene-space square would grow with zoom otherwise.
void MeshSolutionItem::paintProblemGeometry(QPainter* painter, const QRectF& exposedRect)
{
  const FemmProblem& problem = *m_problemGeometry;

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-22: was
  // AppTheme::segmentColor()/arcColor() -- confirmed live on the real
  // transformer model that those (now the palette's blue/teal accents)
  // all but disappear into the Density Plot's own jet colormap fill
  // wherever the visible field is dominated by low values (its own low
  // bands are the same blue family). See AppTheme::densityOverlayColor's
  // own comment for why a single magenta/pink -- for BOTH segments and
  // arcs, not kept separately distinct -- is the fix rather than picking
  // yet another "safer" blue/teal shade.
  // Modified by Claude (Anthropic), noreply@anthropic.com: segPath/arcPath
  // below are stroke-only outlines (moveTo/lineTo per segment, an open
  // subpath per entity) -- but QPainter::drawPath() both fills AND
  // strokes by default, using whatever brush is currently set. Without
  // this, the brush is left over from paintDensity()'s last-drawn band
  // (a solid QColor, no alpha), so the network of crossing/closing
  // segment+arc lines gets implicitly closed and flood-filled with that
  // leftover color wherever the lines happen to enclose an area --
  // confirmed live as the exact root cause of a solid-color area that
  // precisely followed geometry boundaries and swallowed the real
  // per-element density gradient underneath it.
  painter->setBrush(Qt::NoBrush);
  QPen segPen(AppTheme::densityOverlayColor());
  segPen.setCosmetic(true);
  segPen.setWidth(0); // see addNodeItem's (GeometryScene.cpp) comment on width 0 vs the QPen(color) ctor's default of 1
  painter->setPen(segPen);
  QPainterPath segPath;
  for (const FemmSegment& seg : problem.segments) {
    if (seg.hidden)
      continue;
    if (seg.n0 < 0 || seg.n0 >= problem.nodes.size() || seg.n1 < 0 || seg.n1 >= problem.nodes.size())
      continue;
    const FemmNode& n0 = problem.nodes[seg.n0];
    const FemmNode& n1 = problem.nodes[seg.n1];
    // Modified by Claude (Anthropic), noreply@anthropic.com: was an
    // exposedRect.intersects() viewport-culling check here, same idea as
    // the mesh-element culling elsewhere in this file -- but a perfectly
    // horizontal or vertical segment (extremely common: any axis-aligned
    // rectangle boundary) has a bounding QRectF with zero width or zero
    // height, which QRectF::intersects() treats as empty and never
    // intersecting anything, even when the segment plainly crosses the
    // visible area. That silently culled every axis-aligned segment,
    // confirmed directly against a real .ans (4/4 segments, all axis-
    // aligned, none rendered) -- per user report ("the edges-lines of the
    // geometry are not shown... you will only see nodes with no lines").
    // Segments/arcs are always a small fraction of a solved mesh's
    // element count (they're the PRE-mesh geometry), so this culling was
    // never load-bearing for performance the way the mesh-element culling
    // is -- just drop it rather than chase a correct-but-fiddly
    // epsilon-padded rect test.
    segPath.moveTo(n0.x, n0.y);
    segPath.lineTo(n1.x, n1.y);
  }
  painter->drawPath(segPath);

  QPen arcPen(AppTheme::densityOverlayColor());
  arcPen.setCosmetic(true);
  arcPen.setWidth(0); // see addNodeItem's (GeometryScene.cpp) comment on width 0 vs the QPen(color) ctor's default of 1
  painter->setPen(arcPen);
  QPainterPath arcPath;
  for (const FemmArcSegment& arc : problem.arcSegments) {
    if (arc.hidden)
      continue;
    if (arc.n0 < 0 || arc.n0 >= problem.nodes.size() || arc.n1 < 0 || arc.n1 >= problem.nodes.size())
      continue;
    const FemmNode& n0 = problem.nodes[arc.n0];
    const FemmNode& n1 = problem.nodes[arc.n1];
    double cx, cy, R, startAngleDeg;
    if (!arcGeometry(n0.x, n0.y, n1.x, n1.y, arc.arcLength, cx, cy, R, startAngleDeg))
      continue;
    // No exposedRect culling here -- see the identical removal (and its
    // comment) on the segment loop above. This bounding box (2R x 2R) was
    // never actually degenerate the way a segment's could be, but dropped
    // for the same reasoning (segments/arcs are always few relative to a
    // solved mesh's element count) rather than leave an inconsistent,
    // asymmetric special case between the two loops.
    arcPath.moveTo(n0.x, n0.y);
    // Sweep negated -- see GeometryScene.cpp's updateArcItemGeometry for
    // the full writeup (a real, general bug confirmed live with a debug
    // print, not specific to this file): this view also applies a
    // scale(1,-1) y-flip (SolutionWindow's constructor), which the
    // unnegated sweep didn't compensate for, same as the editor's copy of
    // this exact code didn't.
    arcPath.arcTo(cx - R, cy - R, 2 * R, 2 * R, startAngleDeg, -arc.arcLength);
  }
  painter->drawPath(arcPath);

  double screenScale = painter->worldTransform().m11();
  double half = 2.5 / std::max(screenScale, 1e-9);
  QPen nodePen(AppTheme::nodeColor());
  nodePen.setCosmetic(true);
  nodePen.setWidth(0); // see addNodeItem's (GeometryScene.cpp) comment on width 0 vs the QPen(color) ctor's default of 1
  painter->setPen(nodePen);
  painter->setBrush(Qt::NoBrush);
  QRectF cullRect = exposedRect.adjusted(-half, -half, half, half);
  for (const FemmNode& n : problem.nodes) {
    if (!cullRect.contains(n.x, n.y))
      continue;
    painter->drawRect(QRectF(n.x - half, n.y - half, 2 * half, 2 * half));
  }

  if (m_showBlockNames) {
    // Matches femm.rc's IDR_FEMMVIEWTYPE View > Show Block Names and
    // GeometryScene::updateBlockLabelItemGeometry's own text (assigned
    // material's name, or "<None>" for a hole) -- that side uses
    // ItemIgnoresTransformations on a per-label QGraphicsSimpleTextItem to
    // keep the text upright and a constant screen size regardless of the
    // view's zoom/y-flip; this item has no per-label child items to hang
    // that flag on (paint() draws everything itself in one call), so the
    // same effect is done manually here: map each label's scene position
    // through the CURRENT world transform to get its pixel location, then
    // reset to identity before drawing so the text itself is never
    // flipped/scaled by the outer view transform, only positioned by it.
    QTransform sceneToDevice = painter->worldTransform();
    painter->save();
    painter->resetTransform();
    painter->setPen(AppTheme::blockLabelNameColor());
    for (const FemmBlockLabel& b : problem.blockLabels) {
      if (!exposedRect.contains(b.x, b.y))
        continue;
      QString text = (b.blockTypeIndex >= 1 && b.blockTypeIndex <= problem.materialProps.size())
          ? problem.materialProps[b.blockTypeIndex - 1].name
          : QStringLiteral("<None>");
      QPointF devicePos = sceneToDevice.map(QPointF(b.x, b.y));
      painter->drawText(devicePos + QPointF(4, -4), text);
    }
    painter->restore();
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
      painter.fillRect(swatch, m_item->legendBandColor(band));
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

void SolutionGraphicsScene::setShowGrid(bool show)
{
  m_showGrid = show;
  resetViewBackgroundCache();
}

void SolutionGraphicsScene::setGridSize(double size)
{
  m_gridSize = size;
  if (m_showGrid)
    resetViewBackgroundCache();
}

void SolutionGraphicsScene::resetViewBackgroundCache()
{
  // See GeometryScene::resetViewBackgroundCache()'s comment -- this view
  // now uses QGraphicsView::CacheBackground too (see SolutionGraphicsView's
  // constructor), for the same reason: a plain update()/invalidate() call
  // doesn't regenerate that cache, only a view transform/resize or this.
  for (QGraphicsView* view : views()) {
    view->resetCachedContent();
    view->viewport()->update();
  }
}

void SolutionGraphicsScene::drawBackground(QPainter* painter, const QRectF& rect)
{
  QGraphicsScene::drawBackground(painter, rect);
  if (!m_showGrid || m_gridSize <= 0)
    return;

  // See GeometryScene::drawBackground's identical addition/comment -- a
  // new, deliberate feature (classic FEMM has no visual polar grid at
  // all), shown here when the opened file's own Coordinates tag was
  // polar (this window has no Cartesian/Polar UI of its own to change it,
  // just displays whatever the file was saved with).
  if (m_problemGeometry && m_problemGeometry->coordsPolar) {
    double maxR = std::hypot(std::max(std::abs(rect.left()), std::abs(rect.right())),
        std::max(std::abs(rect.top()), std::abs(rect.bottom())));
    int nRings = static_cast<int>(std::ceil(maxR / m_gridSize));
    if (nRings > 2000)
      return;
    QPen gridPen(AppTheme::gridLine());
    gridPen.setCosmetic(true);
    gridPen.setWidthF(1.0);
    painter->setPen(gridPen);
    painter->setBrush(Qt::NoBrush);
    for (int i = 1; i <= nRings; i++) {
      double r = i * m_gridSize;
      painter->drawEllipse(QPointF(0, 0), r, r);
    }
    constexpr int kNumSpokes = 24; // every 15 degrees
    for (int i = 0; i < kNumSpokes; i++) {
      double theta = i * 2.0 * M_PI / kNumSpokes;
      painter->drawLine(QPointF(0, 0), QPointF(maxR * std::cos(theta), maxR * std::sin(theta)));
    }
    return;
  }

  // See this class's header comment -- ported directly from
  // GeometryScene::drawBackground.
  double x0 = std::floor(rect.left() / m_gridSize) * m_gridSize;
  double y0 = std::floor(rect.top() / m_gridSize) * m_gridSize;
  int nx = static_cast<int>(std::ceil(rect.width() / m_gridSize)) + 2;
  int ny = static_cast<int>(std::ceil(rect.height() / m_gridSize)) + 2;
  if ((qint64)nx * (qint64)ny > 200000)
    return;

  double screenScale = painter->worldTransform().m11();
  double r = 1.0 / std::max(screenScale, 1e-9);
  painter->setPen(Qt::NoPen);
  painter->setBrush(AppTheme::gridLine());
  for (int i = 0; i < nx; i++) {
    double x = x0 + i * m_gridSize;
    for (int j = 0; j < ny; j++)
      painter->drawEllipse(QPointF(x, y0 + j * m_gridSize), r, r);
  }
}

SolutionGraphicsView::SolutionGraphicsView(QGraphicsScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
{
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  setResizeAnchor(QGraphicsView::AnchorUnderMouse);
  setMouseTracking(true); // needed for hoveredAt() to fire without a button held
  setFocusPolicy(Qt::StrongFocus); // needed for keyPressEvent (Delete/Escape) to ever fire

  // Modified by Claude (Anthropic), noreply@anthropic.com: per direct user
  // report of dark-theme artifacts trailing the cursor tooltip as it moves
  // -- root cause matches GeometryView's own prior drag-trail bug (see its
  // constructor's comment) exactly: drawBackground's antialiased grid dots
  // (only visible with Show Grid on) get repeatedly recomposited instead of
  // cleanly erased-then-redrawn whenever mouseMoveEvent below calls
  // scene()->invalidate() on the tooltip's vacated rect, which happens on
  // every single mouse move. CacheBackground fixes it the same way it did
  // there: the background is rendered into an offscreen pixmap once and
  // blitted (not recomposited) for subsequent partial repaints.
  setCacheMode(QGraphicsView::CacheBackground);

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
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
  // was 11px in a single line -- per user request ("one above another
  // and a little bit smaller font") now stacked (see onCanvasHovered's
  // tooltipText) at 9px, which also needed line-height tightened a touch
  // (Qt's default leaves noticeably more gap between lines than within
  // a single line at this size) so 5 stacked lines don't look sparse.
  m_cursorTooltip->setStyleSheet(
      "QLabel { background-color: rgba(20, 20, 20, 200); color: white; "
      "padding: 2px 5px; border-radius: 3px; font-size: 9px; line-height: 120%; }");
  m_cursorTooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_cursorTooltip->hide();

  m_legend = new SolutionLegendWidget(viewport());
  m_legend->hide();
}

void SolutionGraphicsView::fitInViewSafe(const QRectF& rect)
{
  if (rect.isEmpty())
    return;
  QGraphicsView::ViewportAnchor prevAnchor = transformationAnchor();
  setTransformationAnchor(QGraphicsView::AnchorViewCenter);
  fitInView(rect, Qt::KeepAspectRatio);
  setTransformationAnchor(prevAnchor);
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

void SolutionGraphicsView::startZoomWindow()
{
  m_zoomWindowActive = true;
  viewport()->setCursor(Qt::CrossCursor);
}

void SolutionGraphicsView::mousePressEvent(QMouseEvent* event)
{
  if (m_zoomWindowActive && event->button() == Qt::LeftButton) {
    m_rubberBandOrigin = event->pos();
    if (!m_rubberBand)
      m_rubberBand = new QRubberBand(QRubberBand::Rectangle, viewport());
    m_rubberBand->setGeometry(QRect(m_rubberBandOrigin, QSize()));
    m_rubberBand->show();
    return;
  }
  if (event->button() == Qt::LeftButton)
    emit clickedAt(mapToScene(event->pos()));
  QGraphicsView::mousePressEvent(event);
}

void SolutionGraphicsView::mouseReleaseEvent(QMouseEvent* event)
{
  if (m_zoomWindowActive && event->button() == Qt::LeftButton) {
    m_zoomWindowActive = false;
    viewport()->unsetCursor();
    QRect selected = m_rubberBand ? m_rubberBand->geometry() : QRect();
    if (m_rubberBand)
      m_rubberBand->hide();
    if (selected.width() > 2 && selected.height() > 2) {
      QRectF sceneRect = mapToScene(selected).boundingRect();
      emit zoomWindowSelected(sceneRect);
    }
    return;
  }
  QGraphicsView::mouseReleaseEvent(event);
}

void SolutionGraphicsView::mouseMoveEvent(QMouseEvent* event)
{
  if (m_zoomWindowActive && m_rubberBand && m_rubberBand->isVisible()) {
    m_rubberBand->setGeometry(QRect(m_rubberBandOrigin, event->pos()).normalized());
    return;
  }
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
  // "changed" there. This view can hold millions of mesh elements, and
  // forcing a full repaint on every single mouse-move (GeometryView.cpp's
  // editor originally worked around the equivalent bug with
  // FullViewportUpdate) would make the reported "everything is slow on
  // large geometries" complaint significantly worse. scene()->invalidate()
  // is the mechanism MinimalViewportUpdate actually respects for "redraw
  // this region even though nothing scene-side changed" -- correctly
  // limited to just the vacated rect, not the whole viewport.
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
  // GeometryView.cpp's own FullViewportUpdate workaround was later found
  // to be causing the exact same "zoom/pan isn't smooth" complaint for
  // the editor too (forced a full repaint on every wheel-zoom and pan,
  // not just hover-move) -- ported this same scene()->invalidate()
  // approach there instead, so both views now share one fix.
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

void SolutionGraphicsView::keyPressEvent(QKeyEvent* event)
{
  if (event->key() == Qt::Key_Delete) {
    emit removeLastContourPointRequested();
    return;
  }
  if (event->key() == Qt::Key_Escape) {
    emit clearContourRequested();
    return;
  }
  QGraphicsView::keyPressEvent(event);
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

  m_scene = new SolutionGraphicsScene(this);
  m_scene->setBackgroundBrush(AppTheme::background());
  m_view = new SolutionGraphicsView(m_scene, this);
  m_view->setRenderHint(QPainter::Antialiasing, true); // see updateAntialiasingForScale()
  // Matches MainWindow's view->scale(1,-1): .ans geometry is in the same
  // math (y-up) convention as .fem.
  m_view->scale(1, -1);
  setCentralWidget(m_view);
  connect(m_view, &SolutionGraphicsView::clickedAt, this, &SolutionWindow::onCanvasClicked);
  connect(m_view, &SolutionGraphicsView::hoveredAt, this, &SolutionWindow::onCanvasHovered);
  connect(m_view, &SolutionGraphicsView::zoomWindowSelected, this, &SolutionWindow::onZoomWindowSelected);
  connect(m_view, &SolutionGraphicsView::removeLastContourPointRequested, this, &SolutionWindow::onRemoveLastContourPointTriggered);
  // Guarded here (not inside onClearContourTriggered itself) so the
  // "Operation > Clear Contour" menu item keeps working unconditionally --
  // only the Escape key path needs to be a silent no-op outside Contour mode.
  connect(m_view, &SolutionGraphicsView::clearContourRequested, this, [this]() {
    if (m_toolMode == SolutionToolMode::Contour)
      onClearContourTriggered();
  });

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
  editMenu->addSeparator();
  editMenu->addAction("&Preferences...", this, &SolutionWindow::onPreferencesTriggered);

  QMenu* zoomMenu = menuBar()->addMenu("&Zoom");
  zoomMenu->addAction("Zoom &In", this, &SolutionWindow::onZoomIn, QKeySequence(Qt::Key_PageUp));
  zoomMenu->addAction("Zoom &Out", this, &SolutionWindow::onZoomOut, QKeySequence(Qt::Key_PageDown));
  zoomMenu->addAction("&Natural", this, &SolutionWindow::onZoomNatural, QKeySequence(Qt::Key_Home));
  zoomMenu->addAction("&Window", this, &SolutionWindow::onZoomWindowTriggered);
  zoomMenu->addAction("&Keyboard", this, &SolutionWindow::onKbdZoomTriggered);
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
  // Modified by Claude (Anthropic), noreply@anthropic.com: per direct
  // user request ("a dialog box when clicking the density plot icon,
  // similar to the classical gui, to set the different quantities and
  // plotting options and range") -- clicking Density Plot now always
  // opens DensityPlotOptionsDialog (which itself now carries the
  // quantity combo, matching femm/cv_DPlotDlg2.h's cvCDPlotDlg2 dialog),
  // instead of silently switching plot mode with quantity/range/greyscale
  // picked separately elsewhere. The formerly-separate "Density Quantity"
  // submenu is gone -- its job is now this dialog's combo box.
  m_densityAction = viewMenu->addAction("&Density Plot...");
  m_densityAction->setCheckable(true);
  plotGroup->addAction(m_densityAction);
  connect(m_densityAction, &QAction::triggered, this, &SolutionWindow::onDensityOptionsTriggered);
  // Matches femm/FemmviewView.cpp's OnCplot -- clicking Contour Plot
  // always opens its options dialog first too, same as Density Plot just
  // above (see that field's comment).
  m_contourAction = viewMenu->addAction("&Contour Plot...");
  m_contourAction->setCheckable(true);
  m_contourAction->setChecked(true);
  plotGroup->addAction(m_contourAction);
  connect(m_contourAction, &QAction::triggered, this, &SolutionWindow::onContourOptionsTriggered);
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
  // Matches femm.rc's IDR_FEMMVIEWTYPE View menu, which nests Grid items
  // directly in View here rather than a separate top-level Grid menu the
  // way IDR_FEMMETYPE (the editor) does -- classic itself is inconsistent
  // between the two views, and MainWindow already mirrors the editor's
  // own layout, so this mirrors the post-processor's.
  QAction* showGridAction = viewMenu->addAction("Show &Grid");
  showGridAction->setCheckable(true);
  showGridAction->setChecked(m_scene->showGrid());
  connect(showGridAction, &QAction::toggled, m_scene, &SolutionGraphicsScene::setShowGrid);
  QAction* snapGridAction = viewMenu->addAction("S&nap Grid");
  snapGridAction->setCheckable(true);
  snapGridAction->setChecked(m_scene->snapToGrid());
  connect(snapGridAction, &QAction::toggled, m_scene, &SolutionGraphicsScene::setSnapToGrid);
  viewMenu->addAction("Se&t Grid...", this, &SolutionWindow::onSetGridTriggered);
  viewMenu->addSeparator();
  viewMenu->addAction("&Circuit Props...", this, &SolutionWindow::onCircuitPropsTriggered);
  viewMenu->addAction("&BH Curves...", this, &SolutionWindow::onBhCurvesTriggered);
  viewMenu->addAction("Problem &Info...", this, &SolutionWindow::onProblemInfoTriggered);
  viewMenu->addSeparator();
  QAction* showBlockNamesAction = viewMenu->addAction("Show &Block Names");
  showBlockNamesAction->setCheckable(true);
  connect(showBlockNamesAction, &QAction::toggled, this, [this](bool on) { if (m_item) m_item->setShowBlockNames(on); });
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
  opMenu->addAction("Clear &Area Selection", this, &SolutionWindow::onClearAreaSelectionTriggered);
  menuBar()->addAction("Plot &X-Y", this, &SolutionWindow::onPlotXYTriggered);
  menuBar()->addAction("&Integrate", this, &SolutionWindow::onIntegrateTriggered);

  // Matches femm.rc's IDR_LEFTBAR -- confirmed in femm/MainFrm.cpp that
  // m_leftbar (Zoom/Pan/Grid) is shared across every doc type's frame,
  // including the postprocessor (FV_toolBar1) -- so the classic Solution
  // Viewer has this toolbar too, not just the geometry editor. Found
  // missing during a full icon-by-icon toolbar audit (the earlier passes
  // were menu/dialog-level only). Reuses the exact same QAction objects
  // the Zoom/View menus above already created (not copies), same pattern
  // as MainWindow::MainWindow's own Navigate toolbar.
  QToolBar* navToolBar = addToolBar("Navigate");
  addToolBar(Qt::LeftToolBarArea, navToolBar);
  navToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
  navToolBar->setIconSize(QSize(20, 20));
  addThemedAction(navToolBar, ":/icons/zoom_in.svg", "Zoom In", "Zoom in", &SolutionWindow::onZoomIn);
  addThemedAction(navToolBar, ":/icons/zoom_out.svg", "Zoom Out", "Zoom out", &SolutionWindow::onZoomOut);
  addThemedAction(navToolBar, ":/icons/zoom_natural.svg", "Natural", "Zoom to fit the entire mesh", &SolutionWindow::onZoomNatural);
  addThemedAction(navToolBar, ":/icons/zoom_window.svg", "Window", "Drag a rectangle to zoom into", &SolutionWindow::onZoomWindowTriggered);
  navToolBar->addSeparator();
  addThemedAction(navToolBar, ":/icons/pan_up.svg", "Scroll Up", "Move the view up", &SolutionWindow::onPanUp);
  addThemedAction(navToolBar, ":/icons/pan_down.svg", "Scroll Down", "Move the view down", &SolutionWindow::onPanDown);
  addThemedAction(navToolBar, ":/icons/pan_left.svg", "Scroll Left", "Move the view left", &SolutionWindow::onPanLeft);
  addThemedAction(navToolBar, ":/icons/pan_right.svg", "Scroll Right", "Move the view right", &SolutionWindow::onPanRight);
  navToolBar->addSeparator();
  navToolBar->addAction(showGridAction);
  showGridAction->setIcon(IconTheme::themedToolIcon(":/icons/show_grid.svg"));
  showGridAction->setToolTip("Show grid points");
  m_themedActions.push_back({ showGridAction, ":/icons/show_grid.svg" });
  navToolBar->addAction(snapGridAction);
  snapGridAction->setIcon(IconTheme::themedToolIcon(":/icons/snap_grid.svg"));
  snapGridAction->setToolTip("Snap new points and drags to the nearest grid point");
  m_themedActions.push_back({ snapGridAction, ":/icons/snap_grid.svg" });
  addThemedAction(navToolBar, ":/icons/set_grid.svg", "Set Grid", "Change the grid spacing", &SolutionWindow::onSetGridTriggered);
  HoverTooltip::installOn(navToolBar);

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
  toolBar->addAction(m_contourAction);
  m_contourAction->setIcon(IconTheme::themedToolIcon(":/icons/contour_plot.svg"));
  m_contourAction->setToolTip("Contour Plot -- draw equipotential (constant A) lines, with a dialog to pick the contour count/range");
  m_themedActions.push_back({ m_contourAction, ":/icons/contour_plot.svg" });
  toolBar->addAction(m_densityAction);
  m_densityAction->setIcon(IconTheme::themedToolIcon(":/icons/density_plot.svg"));
  m_densityAction->setToolTip("Density Plot -- color-shaded field magnitude, with a dialog to pick the quantity/range");
  m_themedActions.push_back({ m_densityAction, ":/icons/density_plot.svg" });
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
    int coordSystem = 0;
    if (AnsxFileIO::readAnsx(ansxPath, m_solution, error, &coordSystem, &m_frequency)) {
      loadedFromAnsx = true;
      m_axisymmetric = (coordSystem == (int)FemmCoordinateType::Axisymmetric);
    }
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
    m_axisymmetric = (problem.problemType == FemmCoordinateType::Axisymmetric);
    m_frequency = problem.frequency;
    // Cache for next time -- best-effort: a failure here (e.g. a
    // read-only directory) shouldn't block viewing the solution we
    // already have loaded, just means no speedup next time.
    QString writeError;
    AnsxFileIO::writeAnsx(ansxPath, ansPath, (int)problem.problemType, (int)problem.lengthUnits,
        problem.frequency, m_solution, writeError);
    // readAns already parsed the geometry (nodes/segments/arcSegments)
    // as a side effect of extracting problemType/lengthUnits/frequency
    // above -- reuse it directly rather than re-parsing the file again
    // below.
    m_problemGeometry = problem;
    m_geometryOverlayError.clear();
  } else {
    // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
    // the .ansx cache only ever stored the solved MESH (that's the whole
    // point -- see this file's header comment), never the original
    // problem geometry, so the fast path above has nothing to reuse the
    // way the slow path does. Best-effort re-read of just the .ans
    // file's geometry header via FemmFileIO::readFem -- already proven
    // safe and fast against a real .ans file elsewhere in this class
    // (onProblemInfoTriggered/onCircuitPropsTriggered/onBhCurvesTriggered
    // all do exactly this), and it tolerates/ignores the (potentially
    // huge) trailing [Solution] mesh section it doesn't need, so this
    // doesn't undermine .ansx's whole "skip the huge mesh reparse" point.
    // A failure here shouldn't block viewing the solution we already
    // have loaded -- it just means no geometry overlay this time. Still
    // recorded (see the status-bar message below) rather than swallowed
    // outright -- a silent gap here previously looked indistinguishable
    // from "there's no geometry to show," which isn't true for any real
    // .fem-backed .ans.
    m_problemGeometry = FemmProblem();
    if (!FemmFileIO::readFem(ansPath, m_problemGeometry, m_geometryOverlayError))
      m_problemGeometry = FemmProblem(); // readFem may have partially populated it before failing
  }

  qint64 elapsedMs = timer.elapsed();

  m_spatialIndexBuilt = false; // m_solution just got replaced -- see buildSpatialIndex()'s comment
  m_scene->clear();
  m_contourVisual = nullptr; // clear() above already deleted it
  m_contourPoints.clear();
  m_item = new MeshSolutionItem(&m_solution);
  m_item->setProblemGeometry(&m_problemGeometry);
  m_scene->setProblemGeometry(&m_problemGeometry);
  m_scene->addItem(m_item);
  QRectF itemBounds = m_item->boundingRect();
  m_view->fitInViewSafe(itemBounds);
  m_scene->setGridSize(niceIntegerGridSize(itemBounds));
  m_view->updateAntialiasingForScale();
  m_view->setLegendItem(m_item);
  m_currentPath = ansPath;

  QString statusMsg = QString("%1 -- %2 mesh nodes, %3 elements, |B| %4 to %5 T (loaded via %6 in %7 ms)")
                           .arg(path)
                           .arg(m_solution.nodes.size())
                           .arg(m_solution.elements.size())
                           .arg(m_solution.bMagMin, 0, 'g', 4)
                           .arg(m_solution.bMagMax, 0, 'g', 4)
                           .arg(loadedFromAnsx ? ".ansx" : ".ans")
                           .arg(elapsedMs);
  if (!m_geometryOverlayError.isEmpty())
    statusMsg += QString(" -- geometry overlay unavailable: %1").arg(m_geometryOverlayError);
  statusBar()->showMessage(statusMsg);
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
  QString statusText, tooltipText;
  if (elem < 0) {
    statusText = QString("x = %1, y = %2").arg(scenePos.x(), 0, 'g', 6).arg(scenePos.y(), 0, 'g', 6);
    tooltipText = statusText;
  } else {
    std::complex<double> A = interpolateA(scenePos, elem);
    const MeshSolutionElement& e = m_solution.elements[elem];
    double bMag = std::hypot(std::hypot(e.B1re, e.B1im), std::hypot(e.B2re, e.B2im));
    // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
    // |H| and |Js+Je| added to the live hover readout per user request
    // ("show the values when hovering") -- previously only available via
    // the Point Properties tool's click-to-open dialog (onCanvasClicked's
    // SolutionToolMode::Point case, below), which has the identical
    // formula; duplicated rather than shared since that case also breaks
    // H/J into re/im components for the dialog's extra rows, which this
    // tooltip has no room for.
    //
    // Modified by Claude (Anthropic), noreply@anthropic.com: now shared
    // after all, via computeElementH() -- see that function's own
    // comment for why the muX/muY formula alone was wrong for a
    // nonlinear material.
    double h1re, h1im, h2re, h2im;
    computeElementH(e, m_solution, h1re, h1im, h2re, h2im);
    double hMag = std::hypot(std::hypot(h1re, h1im), std::hypot(h2re, h2im));
    double jMag = std::hypot(e.jRe, e.jIm);
    // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
    // per user correction ("for the A you do not have a unit") -- classic
    // FEMM (femm/FemmviewView.cpp's DisplayPointProperties) labels this
    // raw solved nodal value differently by coordinate system: "A ...
    // Wb/m" for planar, "Flux ... Wb" for axisymmetric -- same value
    // (MeshSolutionNode::Are/Aim, interpolated above), no conversion, just
    // the right label/unit for whichever this solution is (m_axisymmetric,
    // set once in openAnsFile).
    const char* aLabel = m_axisymmetric ? "Flux" : "A";
    const char* aUnit = m_axisymmetric ? "Wb" : "Wb/m";
    statusText = QString("x = %1, y = %2   |B| = %3 T   |H| = %4 A/m   |Js+Je| = %5 MA/m^2   %6 = %7 %8")
                     .arg(scenePos.x(), 0, 'g', 6)
                     .arg(scenePos.y(), 0, 'g', 6)
                     .arg(bMag, 0, 'g', 4)
                     .arg(hMag, 0, 'g', 4)
                     .arg(jMag, 0, 'g', 4)
                     .arg(aLabel)
                     .arg(A.real(), 0, 'g', 4)
                     .arg(aUnit);
    // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
    // the floating cursor tooltip (unlike the status bar, which stays a
    // single conventional line) is stacked one value per line per user
    // request ("have them one above another") -- the single-line version
    // was wide enough to run off the edge of the view at typical window
    // sizes.
    tooltipText = QString("x = %1, y = %2\n|B| = %3 T\n|H| = %4 A/m\n|Js+Je| = %5 MA/m^2\n%6 = %7 %8")
                      .arg(scenePos.x(), 0, 'g', 6)
                      .arg(scenePos.y(), 0, 'g', 6)
                      .arg(bMag, 0, 'g', 4)
                      .arg(hMag, 0, 'g', 4)
                      .arg(jMag, 0, 'g', 4)
                      .arg(aLabel)
                      .arg(A.real(), 0, 'g', 4)
                      .arg(aUnit);
  }
  m_positionLabel->setText(statusText);
  m_view->setTooltipText(tooltipText);
}

void SolutionWindow::onPointToolTriggered()
{
  // Matches femm/FemmviewView.cpp's OnMenuContour/OnMenuPoint: leaving
  // Area mode (EditAction==2) clears any block-label selection rather
  // than leaving a stale highlight from a tool that's no longer active.
  if (m_toolMode == SolutionToolMode::Area && m_item)
    m_item->clearBlockLabelSelection();
  m_toolMode = SolutionToolMode::Point;
  statusBar()->showMessage("Point Properties: click a point on the mesh.");
}

void SolutionWindow::onContourToolTriggered()
{
  if (m_toolMode == SolutionToolMode::Area && m_item)
    m_item->clearBlockLabelSelection();
  m_toolMode = SolutionToolMode::Contour;
  statusBar()->showMessage("Contours: click points to build a contour, then Operation > Finish Contour.");
}

// femm/FemmeView.cpp's OnKeyDown, while drawing a contour (EditAction==1,
// this app's SolutionToolMode::Contour): Delete removes the last-placed
// point and Escape clears the whole contour -- both now wired up, see
// SolutionGraphicsView::keyPressEvent + onRemoveLastContourPointTriggered/
// onClearContourTriggered. Still a known, documented gap: Shift opens
// CBendContourDlg ("Bend Contour": an angle + angle-step pair that
// reshapes the straight-line contour between its first and last points
// into a circular arc through them, via CFemmviewDoc::BendContour --
// useful for sampling along an air-gap circle in a rotating machine
// without clicking dozens of points by hand). Not attempted here -- it's
// a real, self-contained new feature (its own small dialog + a
// point-set transform), not a partial version of one already in place.

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
    //
    // Modified by Claude (Anthropic), noreply@anthropic.com: now shared
    // via computeElementH() -- see that function's own comment for why
    // the muX/muY formula alone was wrong for a nonlinear material.
    double h1re, h1im, h2re, h2im;
    computeElementH(e, m_solution, h1re, h1im, h2re, h2im);
    double hMag = std::hypot(std::hypot(h1re, h1im), std::hypot(h2re, h2im));
    double jMag = std::hypot(e.jRe, e.jIm);
    // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
    // same A/Flux label+unit fix as onCanvasHovered -- see that method's
    // comment.
    QString aLabel = m_axisymmetric ? "Flux (re, im)" : "A (re, im)";
    QString aUnit = m_axisymmetric ? "Wb" : "Wb/m";

    // Matches femm/FemmviewView.cpp's DisplayPointProperties, which also
    // shows mu_x/mu_y and, for a permanent-magnet material (Hc != 0), the
    // B.H energy product -- resolved the same way BlockLabelPropDialog
    // resolves a label's material, via m_problemGeometry (this window's
    // parsed .fem header, already loaded for the geometry overlay).
    // Deliberately NOT shown here: "E" (energy density, needs the
    // material's possibly-nonlinear B-H curve integral -- see
    // elementQuantity's own comment on that same limitation) and
    // "Winding Fill %" (classic's u.ff comes from CBlockLabel::
    // FillFactor, a per-LABEL field this app's FemmBlockLabel doesn't
    // carry at all yet -- a real, separate gap, not attempted here with
    // the wrong data source).
    // Mutually exclusive ONLY for a DC solution, matching classic's own
    // if(Hc==0){mu}else{B.H} exactly (a permanent-magnet material shows
    // B.H instead of mu_x/mu_y there) -- classic's AC/harmonic branch has
    // no B.H term at all and shows mu_x/mu_y unconditionally, so this
    // stays gated on m_frequency, not just Hc.
    QString muLine, bhLine;
    if (e.lbl >= 0 && e.lbl < m_problemGeometry.blockLabels.size()) {
      int matIdx = m_problemGeometry.blockLabels[e.lbl].blockTypeIndex - 1;
      if (matIdx >= 0 && matIdx < m_problemGeometry.materialProps.size()) {
        const FemmMaterialProp& mat = m_problemGeometry.materialProps[matIdx];
        if (m_frequency == 0 && mat.Hc != 0) {
          constexpr double kMuo = 1.2566370614359173e-6;
          double bh = std::abs(std::complex<double>(e.B1re, e.B1im) * std::complex<double>(h1re, h1im)
              + std::complex<double>(e.B2re, e.B2im) * std::complex<double>(h2re, h2im));
          bhLine = QString("%1 J/m^3 (%2 MGOe)").arg(bh, 0, 'g', 6).arg(bh * kMuo * 100.0, 0, 'g', 6);
        } else {
          muLine = QString("%1, %2 (rel)").arg(mat.muX, 0, 'g', 6).arg(mat.muY, 0, 'g', 6);
        }
      }
    }

    QDialog dlg(this);
    dlg.setWindowTitle("Point Properties");
    auto* form = new QFormLayout(&dlg);
    form->addRow("x, y:", new QLabel(QString("%1, %2").arg(scenePos.x(), 0, 'g', 6).arg(scenePos.y(), 0, 'g', 6)));
    form->addRow(aLabel + ":", new QLabel(QString("%1, %2 %3").arg(A.real(), 0, 'g', 6).arg(A.imag(), 0, 'g', 6).arg(aUnit)));
    form->addRow("B1 (re, im):", new QLabel(QString("%1, %2").arg(e.B1re, 0, 'g', 6).arg(e.B1im, 0, 'g', 6)));
    form->addRow("B2 (re, im):", new QLabel(QString("%1, %2").arg(e.B2re, 0, 'g', 6).arg(e.B2im, 0, 'g', 6)));
    form->addRow("|B|:", new QLabel(QString("%1 T").arg(bMag, 0, 'g', 6)));
    form->addRow("H1 (re, im):", new QLabel(QString("%1, %2").arg(h1re, 0, 'g', 6).arg(h1im, 0, 'g', 6)));
    form->addRow("H2 (re, im):", new QLabel(QString("%1, %2").arg(h2re, 0, 'g', 6).arg(h2im, 0, 'g', 6)));
    form->addRow("|H|:", new QLabel(QString("%1 A/m").arg(hMag, 0, 'g', 6)));
    if (!muLine.isEmpty())
      form->addRow("mu_x, mu_y:", new QLabel(muLine));
    if (!bhLine.isEmpty())
      form->addRow("B.H:", new QLabel(bhLine));
    form->addRow("Js+Je (re, im):", new QLabel(QString("%1, %2").arg(e.jRe, 0, 'g', 6).arg(e.jIm, 0, 'g', 6)));
    form->addRow("|Js+Je|:", new QLabel(QString("%1 MA/m^2").arg(jMag, 0, 'g', 6)));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    form->addRow(buttons);
    appendOutput(QString("Point: x=%1, y=%2  %12=(%3, %4)  B1=(%5, %6)  B2=(%7, %8)  |B|=%9 T  |H|=%10 A/m  |Js+Je|=%11 MA/m^2")
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
                      .arg(jMag, 0, 'g', 6)
                      .arg(m_axisymmetric ? "Flux" : "A"));
    dlg.exec();
    break;
  }
  case SolutionToolMode::Contour:
    m_contourPoints.push_back(scenePos);
    updateContourVisual();
    statusBar()->showMessage(QString("Contour: %1 point(s). Operation > Finish Contour when done.").arg(m_contourPoints.size()));
    break;
  case SolutionToolMode::Area: {
    // Matches femm/FemmviewView.cpp's OnLButtonUp (EditAction==2): a click
    // TOGGLES the clicked block label's selection (highlighted on screen
    // via paintSelectedBlocks -- see MeshSolutionItem::
    // toggleBlockLabelSelected's comment) rather than instantly computing
    // and popping up a result. Multiple regions can be selected at once;
    // Integrate reports the sum over the whole current selection, same
    // split as classic's own select-then-Integrate flow.
    int elem = findContainingElement(scenePos);
    if (elem < 0) {
      statusBar()->showMessage("No mesh element at that point.");
      return;
    }
    int lbl = m_solution.elements[elem].lbl;
    m_item->toggleBlockLabelSelected(lbl);
    int n = m_item->selectedBlockLabels().size();
    statusBar()->showMessage(n > 0
            ? QString("Areas: %1 block(s) selected. Integrate to compute, or Operation > Clear Area Selection.").arg(n)
            : "Areas: selection cleared.");
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

namespace {
// Matches CircuitAnalysis.cpp's identically-named file-local helper
// (femm/FemmviewDoc.cpp's LengthConv[] table) -- kept as its own small
// copy rather than shared, consistent with this codebase's existing
// precedent of AnsFileIO.cpp's kLengthConv doing the same.
double lengthConvToMeters(FemmLengthUnits u)
{
  switch (u) {
  case FemmLengthUnits::Inches: return 0.0254;
  case FemmLengthUnits::Millimeters: return 0.001;
  case FemmLengthUnits::Centimeters: return 0.01;
  case FemmLengthUnits::Meters: return 1.0;
  case FemmLengthUnits::Mils: return 0.0000254;
  case FemmLengthUnits::Microns: return 0.000001;
  }
  return 1.0;
}

// Matches ProblemPropertiesDialog.cpp's m_lengthUnits combo text (same
// enum order) -- own small copy of MainWindow.cpp's identically-named
// helper, consistent with this file's lengthConvToMeters above.
QString lengthUnitsName(FemmLengthUnits u)
{
  switch (u) {
  case FemmLengthUnits::Inches: return "Inches";
  case FemmLengthUnits::Millimeters: return "Millimeters";
  case FemmLengthUnits::Centimeters: return "Centimeters";
  case FemmLengthUnits::Meters: return "Meters";
  case FemmLengthUnits::Mils: return "Mils";
  case FemmLengthUnits::Microns: return "Microns";
  }
  return QString();
}
}

void SolutionWindow::showContourIntegral()
{
  if (m_contourPoints.size() < 2) {
    QMessageBox::information(this, "Contour Properties", "Click at least two points first (Operation > Contours).");
    return;
  }

  double length = 0; // scene units (== the problem's own LengthUnits)
  for (int i = 1; i < m_contourPoints.size(); i++) {
    QPointF d = m_contourPoints[i] - m_contourPoints[i - 1];
    length += std::hypot(d.x(), d.y());
  }
  double lc = lengthConvToMeters(m_problemGeometry.lengthUnits);
  double lengthM = length * lc;

  int elemStart = findContainingElement(m_contourPoints.first());
  int elemEnd = findContainingElement(m_contourPoints.last());
  bool endpointsOk = elemStart >= 0 && elemEnd >= 0;
  std::complex<double> deltaA(0, 0);
  if (endpointsOk) {
    std::complex<double> aStart = interpolateA(m_contourPoints.first(), elemStart);
    std::complex<double> aEnd = interpolateA(m_contourPoints.last(), elemEnd);
    deltaA = aEnd - aStart;
  }

  QDialog dlg(this);
  dlg.setWindowTitle("Contour Properties");
  auto* layout = new QVBoxLayout(&dlg);
  auto* form = new QFormLayout;
  form->addRow("Points:", new QLabel(QString::number(m_contourPoints.size())));
  form->addRow("Length:", new QLabel(QString("%1").arg(length, 0, 'g', 6)));

  // Matches femm/LIntDlg.cpp's CLIntDlg combo (femm/FemmviewDoc.cpp's
  // LineIntegral) -- only B.n and Contour Length are offered here: both
  // reduce to a closed-form Stokes'-theorem shortcut from the two
  // endpoints' A values (B.n) or the already-computed segment lengths
  // (Contour Length/Surface Area), needing no new machinery. H.t, Force
  // from Stress Tensor, Torque from Stress Tensor, and (B.n)^2 all
  // instead need classic's fine per-sample-point numerical integration
  // along the contour (d_LineIntegralPoints subdivisions per segment,
  // locating the containing mesh element at each one) -- a real, scoped
  // follow-up, not attempted here as a partial/rushed version.
  auto* typeCombo = new QComboBox(&dlg);
  typeCombo->addItem("B.n");
  typeCombo->addItem("Contour length");
  form->addRow("Integral:", typeCombo);

  auto* resultLabel = new QLabel(&dlg);
  resultLabel->setWordWrap(true);
  resultLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  form->addRow(resultLabel);
  layout->addLayout(form);

  // depthM: femmqt's FemmProblem::depth is in the problem's own native
  // LengthUnits (same representation CircuitAnalysis.cpp's flux-linkage
  // calc already established -- see its "problem.depth * lc" line), so
  // it needs the same *lc scaling as length does to become meters.
  double depthM = m_problemGeometry.depth * lc;
  auto updateResult = [this, endpointsOk, deltaA, lengthM, lc, depthM](int index) {
    if (index == 0) {
      // B.n: matches LineIntegral's inttype==0 exactly, including its own
      // differing sign convention between planar and axisymmetric.
      if (!endpointsOk) {
        return QString("n/a (an endpoint is outside the mesh)");
      }
      if (!m_axisymmetric) {
        std::complex<double> flux = -deltaA * depthM;
        std::complex<double> avgBn = lengthM * depthM != 0 ? flux / (lengthM * depthM) : std::complex<double>(0, 0);
        return QString("Normal flux = %1, %2 Webers\nAverage B.n = %3, %4 Tesla")
            .arg(flux.real(), 0, 'g', 6).arg(flux.imag(), 0, 'g', 6)
            .arg(avgBn.real(), 0, 'g', 6).arg(avgBn.imag(), 0, 'g', 6);
      } else {
        double area = 0; // Pappus-theorem swept surface area of the axisymmetric contour
        for (int i = 1; i < m_contourPoints.size(); i++) {
          QPointF d = m_contourPoints[i] - m_contourPoints[i - 1];
          area += M_PI * (m_contourPoints[i - 1].x() + m_contourPoints[i].x()) * std::hypot(d.x(), d.y());
        }
        area *= lc * lc;
        std::complex<double> flux = deltaA;
        std::complex<double> avgBn = area != 0 ? flux / area : std::complex<double>(0, 0);
        return QString("Normal flux = %1, %2 Webers\nAverage B.n = %3, %4 Tesla")
            .arg(flux.real(), 0, 'g', 6).arg(flux.imag(), 0, 'g', 6)
            .arg(avgBn.real(), 0, 'g', 6).arg(avgBn.imag(), 0, 'g', 6);
      }
    } else {
      double surfaceArea = m_axisymmetric ? 0.0 : lengthM * depthM;
      if (m_axisymmetric) {
        for (int i = 1; i < m_contourPoints.size(); i++) {
          QPointF d = m_contourPoints[i] - m_contourPoints[i - 1];
          surfaceArea += M_PI * (m_contourPoints[i - 1].x() + m_contourPoints[i].x()) * std::hypot(d.x(), d.y());
        }
        surfaceArea *= lc * lc;
      }
      return QString("Contour length = %1 meters\nSurface Area = %2 meter^2").arg(lengthM, 0, 'g', 6).arg(surfaceArea, 0, 'g', 6);
    }
  };
  connect(typeCombo, &QComboBox::currentIndexChanged, &dlg, [resultLabel, updateResult](int index) { resultLabel->setText(updateResult(index)); });
  resultLabel->setText(updateResult(0));

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  layout->addWidget(buttons);
  appendOutput(QString("Contour: points=%1  length=%2  %3").arg(m_contourPoints.size()).arg(length, 0, 'g', 6).arg(updateResult(typeCombo->currentIndex())));
  dlg.exec();
}

void SolutionWindow::showAreaIntegral()
{
  // Matches femm/BlockInt.cpp's IDD_BLOCKINT combo exactly (item text and
  // order decoded from femm.rc's DLGINIT), reached via Integrate the same
  // way classic's own OnMenuIntegrate does when EditAction==2 with a
  // nonempty block selection. Per direct user request ("add a drop down
  // list for the calculated quantity same as in classical gui").
  //
  // Only 4 of the 17 are actually computed -- Block cross-section area,
  // Block volume, Total current, and Integral of B over block -- because
  // those are the ones femm/FemmviewDoc.cpp's BlockIntegral() computes as
  // a plain per-element sum of data this app already has (area, depth/
  // Pappus-theorem revolution volume, jRe/jIm, B1/B2). Verified against
  // BlockIntegral()'s own source, case by case, rather than guessed:
  // case 5 (area) is `a`; case 10 (volume) is `a*Depth` or `a*2*pi*R`;
  // case 7 (total current) is `a*J`; cases 8/9 (integral of B) are
  // `a*Depth*B1`/`a*Depth*B2` (or the axisymmetric Pappus equivalent) --
  // none of the four have an extra AC/DC scaling factor the way e.g. case
  // 4 (Resistive losses) does. The other 13 (A.J, A, energy/coenergy,
  // hysteresis/eddy/resistive/total losses, Lorentz force/torque, Weighted
  // Stress Tensor force/torque, R^2) need either a proper per-node
  // polynomial integral (BlockIntegral's PlnInt/AxiInt, not just area
  // times an element average -- exact for a single linear field like A
  // alone, but NOT for a product of two, like A.J), conductivity/
  // nonlinear-BH-curve/permanent-magnet data, or the Weighted Stress
  // Tensor mask -- same documented gap as Circuit Props, BH Curves, and
  // the Line Integral's H.t/Force/Torque types. Rather than risk a
  // plausible-looking but subtly wrong number, those show a plain
  // "not available" message instead.
  if (!m_item || !m_item->hasBlockLabelSelection()) {
    QMessageBox::information(this, "Area", "No area selected -- click inside one or more regions in Areas mode first.");
    return;
  }
  const QSet<int>& labels = m_item->selectedBlockLabels();
  double lc = lengthConvToMeters(m_problemGeometry.lengthUnits);
  double depthM = m_problemGeometry.depth * lc;

  double totalAreaM2 = 0;
  double volumeM3 = 0;
  int count = 0;
  std::complex<double> totalCurrent(0, 0);
  std::complex<double> intB1(0, 0), intB2(0, 0); // Tesla*meter^3, x/y or r/z component

  for (const MeshSolutionElement& e : m_solution.elements) {
    if (!labels.contains(e.lbl))
      continue;
    if (e.p0 < 0 || e.p0 >= m_solution.nodes.size() || e.p1 < 0 || e.p1 >= m_solution.nodes.size() || e.p2 < 0 || e.p2 >= m_solution.nodes.size())
      continue;
    const MeshSolutionNode& n0 = m_solution.nodes[e.p0];
    const MeshSolutionNode& n1 = m_solution.nodes[e.p1];
    const MeshSolutionNode& n2 = m_solution.nodes[e.p2];
    double areaM2 = triangleArea(QPointF(n0.x, n0.y), QPointF(n1.x, n1.y), QPointF(n2.x, n2.y)) * lc * lc;
    totalAreaM2 += areaM2;
    count++;

    totalCurrent += areaM2 * std::complex<double>(e.jRe, e.jIm);

    double weight = depthM;
    if (m_axisymmetric) {
      double rCtrM = (n0.x + n1.x + n2.x) / 3.0 * lc;
      weight = 2.0 * M_PI * rCtrM;
    }
    intB1 += (areaM2 * weight) * std::complex<double>(e.B1re, e.B1im);
    intB2 += (areaM2 * weight) * std::complex<double>(e.B2re, e.B2im);
    volumeM3 += areaM2 * weight;
  }

  QDialog dlg(this);
  dlg.setWindowTitle("Area Properties");
  auto* layout = new QVBoxLayout(&dlg);
  auto* form = new QFormLayout;
  QStringList labelList;
  for (int lbl : labels)
    labelList << QString::number(lbl);
  form->addRow("Block labels selected:", new QLabel(QString("%1 (%2)").arg(labels.size()).arg(labelList.join(", "))));
  form->addRow("Elements:", new QLabel(QString::number(count)));

  const QStringList kQuantities = {
    "A . J", "A", "Magnetic field energy",
    "Hysteresis, Laminated eddy, or Proximity effect", "Resistive losses",
    "Block cross-section area", "Total losses", "Total current",
    "Integral of B over block", "Block volume", "Lorentz force (J x B)",
    "Lorentz torque (r x J x B)", "Magnetic field coenergy",
    "Force via Weighted Stress Tensor", "Torque via Weighted Stress Tensor",
    "R^2 (i.e. Moment of Inertia / Density)", "Total Loss Density",
  };
  auto* typeCombo = new QComboBox(&dlg);
  typeCombo->addItems(kQuantities);
  typeCombo->setCurrentIndex(5); // Block cross-section area -- matches the old default (this was the only quantity before)
  form->addRow("Integral:", typeCombo);

  auto* resultLabel = new QLabel(&dlg);
  resultLabel->setWordWrap(true);
  resultLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  form->addRow(resultLabel);
  layout->addLayout(form);

  QString axis1 = m_axisymmetric ? "r" : "x";
  QString axis2 = m_axisymmetric ? "z" : "y";
  bool isAc = m_frequency != 0;
  auto updateResult = [this, totalAreaM2, volumeM3, totalCurrent, intB1, intB2, axis1, axis2, isAc](int index) -> QString {
    switch (index) {
    case 5: // Block cross-section area
      return QString("%1 meter^2").arg(totalAreaM2, 0, 'g', 6);
    case 7: // Total current
      return isAc ? QString("%1 Amps").arg(complexToString(totalCurrent))
                  : QString("%1 Amps").arg(totalCurrent.real(), 0, 'g', 6);
    case 8: // Integral of B over block
      if (isAc)
        return QString("%1-component: %2 Tesla meter^3\n%3-component: %4 Tesla meter^3")
            .arg(axis1, complexToString(intB1), axis2, complexToString(intB2));
      return QString("%1-component: %2 Tesla meter^3\n%3-component: %4 Tesla meter^3")
          .arg(axis1).arg(intB1.real(), 0, 'g', 6).arg(axis2).arg(intB2.real(), 0, 'g', 6);
    case 9: // Block volume
      return QString("%1 meter^3").arg(volumeM3, 0, 'g', 6);
    default:
      return "Not available in this version -- needs per-node potential/current data for a "
             "proper polynomial integral, material conductivity/nonlinear-BH-curve/permanent-"
             "magnet data, or the Weighted Stress Tensor mask, none of which this app currently "
             "extracts from .ans.";
    }
  };
  connect(typeCombo, &QComboBox::currentIndexChanged, &dlg, [resultLabel, updateResult](int index) { resultLabel->setText(updateResult(index)); });
  resultLabel->setText(updateResult(typeCombo->currentIndex()));

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  layout->addWidget(buttons);
  appendOutput(QString("Area: %1 block(s) [%2]  elements=%3  %4: %5")
                    .arg(labels.size())
                    .arg(labelList.join(", "))
                    .arg(count)
                    .arg(kQuantities[typeCombo->currentIndex()])
                    .arg(updateResult(typeCombo->currentIndex())));
  dlg.exec();
}

void SolutionWindow::onClearAreaSelectionTriggered()
{
  if (m_item)
    m_item->clearBlockLabelSelection();
  statusBar()->showMessage("Area selection cleared.");
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

void SolutionWindow::onRemoveLastContourPointTriggered()
{
  // femm/FemmeView.cpp's OnKeyDown: Delete removes only the last-placed
  // contour point (clears entirely if just one point was left), letting a
  // misclick be corrected without restarting the whole contour.
  if (m_toolMode != SolutionToolMode::Contour || m_contourPoints.isEmpty())
    return;
  m_contourPoints.removeLast();
  updateContourVisual();
  statusBar()->showMessage(QString("Contour: %1 point(s). Operation > Finish Contour when done.").arg(m_contourPoints.size()));
}

void SolutionWindow::onPlotXYTriggered()
{
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

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-26: per
  // user report ("I just see the numbers") -- this used to only build the
  // tab-separated text below. Now also collects the same samples into
  // plain QVector<double>s so PlotXYChartWidget can draw them, and the
  // text is kept (as CSV, comma- not tab-separated, for the Export CSV
  // button) rather than just for display.
  // Known, documented gap: classic's real Plot X-Y quantity combo
  // (femm/xyplotdlg.cpp's CXYPlotDlg::OnInitDialog) offers Potential
  // (=|A|), |B|, B.n, B.t, |H|, H.n, H.t, and (AC only) J_eddy/Js+J_eddy
  // -- 7-9 choices, not just the 2 (|A|, |B|) plotted here. B.n/B.t/H.n/
  // H.t need a per-sample-point normal/tangent projection (using each
  // contour segment's own local direction) that isn't built yet; adding
  // them is a real, scoped follow-up, not attempted here as a partial
  // version that only covers some of the missing quantities.
  const QString qtyALabel = "|A|";
  const QString qtyBLabel = "|B|";
  const QString qtyAUnit = m_axisymmetric ? "Wb" : "Wb/m";
  const QString qtyBUnit = "T";

  QVector<double> arcLen, xs, ys, aMags, bMags;
  QString csv = QString("arc length,x,y,%1,%2\n").arg(qtyALabel, qtyBLabel);
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
    arcLen.push_back(target);
    xs.push_back(pt.x());
    ys.push_back(pt.y());
    aMags.push_back(aMag);
    bMags.push_back(bMag);
    csv += QString("%1,%2,%3,%4,%5\n").arg(target, 0, 'g', 6).arg(pt.x(), 0, 'g', 6).arg(pt.y(), 0, 'g', 6).arg(aMag, 0, 'g', 6).arg(bMag, 0, 'g', 6);
  }

  QDialog dlg(this);
  dlg.setWindowTitle("Plot X-Y along Contour");
  dlg.resize(600, 500);
  auto* layout = new QVBoxLayout(&dlg);

  auto* tabs = new QTabWidget(&dlg);

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-26: per
  // direct user request ("select what to plot, only show one plot based
  // on the selection") -- was always both |A| and |B| stacked; a combo
  // box now picks which one PlotXYChartWidget actually draws.
  auto* chartTab = new QWidget(tabs);
  auto* chartLayout = new QVBoxLayout(chartTab);
  auto* quantityCombo = new QComboBox(chartTab);
  quantityCombo->addItem(QString("%1 (%2)").arg(qtyALabel, qtyAUnit));
  quantityCombo->addItem(QString("%1 (%2)").arg(qtyBLabel, qtyBUnit));
  chartLayout->addWidget(quantityCombo);
  auto* chart = new PlotXYChartWidget(arcLen, aMags, bMags, chartTab);
  chart->setTitles(QString("%1 along contour").arg(qtyALabel), QString("%1 along contour").arg(qtyBLabel));
  chartLayout->addWidget(chart);
  connect(quantityCombo, &QComboBox::currentIndexChanged, chart, [chart](int index) {
    chart->setQuantity(index == 0 ? PlotXYChartWidget::Quantity::AMag : PlotXYChartWidget::Quantity::BMag);
  });
  tabs->addTab(chartTab, "Chart");

  QString tableText = QString("arc length\tx\ty\t%1\t%2\n").arg(qtyALabel, qtyBLabel);
  for (int i = 0; i < arcLen.size(); i++)
    tableText += QString("%1\t%2\t%3\t%4\t%5\n").arg(arcLen[i], 0, 'g', 6).arg(xs[i], 0, 'g', 6).arg(ys[i], 0, 'g', 6).arg(aMags[i], 0, 'g', 6).arg(bMags[i], 0, 'g', 6);
  auto* table = new QPlainTextEdit(tableText, tabs);
  table->setReadOnly(true);
  table->setLineWrapMode(QPlainTextEdit::NoWrap);
  tabs->addTab(table, "Table");

  layout->addWidget(tabs);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
  QPushButton* exportCsvButton = buttons->addButton("Export CSV...", QDialogButtonBox::ActionRole);
  QPushButton* exportPngButton = buttons->addButton("Export PNG...", QDialogButtonBox::ActionRole);
  connect(exportCsvButton, &QPushButton::clicked, &dlg, [&dlg, csv]() {
    QString path = QFileDialog::getSaveFileName(&dlg, "Export Plot X-Y as CSV", QString(), "CSV Files (*.csv)");
    if (path.isEmpty())
      return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(csv.toUtf8()) < 0)
      QMessageBox::warning(&dlg, "Export CSV", "Could not write \"" + path + "\".");
  });
  connect(exportPngButton, &QPushButton::clicked, &dlg, [&dlg, chart]() {
    QString path = QFileDialog::getSaveFileName(&dlg, "Export Plot X-Y as PNG", QString(), "PNG Files (*.png)");
    if (path.isEmpty())
      return;
    if (!chart->grab().save(path, "PNG"))
      QMessageBox::warning(&dlg, "Export PNG", "Could not write \"" + path + "\".");
  });
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  layout->addWidget(buttons);
  dlg.exec();
}

void SolutionWindow::onIntegrateTriggered()
{
  // femm.rc's "Integrate" is a standalone command distinct from "Finish
  // Contour" (which also shows the same result) -- kept as a thin alias
  // onto the same contour-integral logic when a contour is what's active,
  // since both operate on "the contour currently drawn." Matches classic's
  // own OnMenuIntegrate branching (EditAction==2 && bBlocksAreSelected)
  // when the Areas tool has an active multi-block selection instead --
  // see showAreaIntegral's comment for what's ported vs still deferred
  // from classic's full 17-quantity Block Integral combo (IDD_BLOCKINT).
  if (m_toolMode == SolutionToolMode::Area && m_item && m_item->hasBlockLabelSelection())
    showAreaIntegral();
  else
    showContourIntegral();
}

// Known, documented gap found during the femm.rc dialog sweep: femm/
// FemmviewView.cpp's OnMenuIntegrate has a second branch (GapIntegral,
// IDD_GAPINTEGRAL/IDD_GAPPLOTDLG) reached "for any EditAction so long as
// no other integration region has been defined and at least one Air Gap
// Element exists" -- Torque/Force/Flux-linkage/etc. integrated via
// Arkkio's method along a circular Air Gap Element arc, the standard way
// classic FEMM gets rotating-machine torque without a full Maxwell-
// stress-tensor mask. Not implemented here: unlike the already-deferred
// stress-tensor Force/Torque (missing math only), this needs a new
// geometry primitive from scratch -- "Air Gap Element" doesn't exist
// anywhere in FemmProblem/GeometryScene today, so it's an editor-side
// addition (a new entity type, its own properties dialog, PSLG/mesh
// marker encoding) plus the Arkkio integral itself. A real future round,
// not a menu-item-sized addition.

void SolutionWindow::onReloadTriggered()
{
  if (m_currentPath.isEmpty()) {
    QMessageBox::information(this, "Reload", "No solution loaded.");
    return;
  }
  openAnsFile(m_currentPath);
}

void SolutionWindow::onDensityOptionsTriggered()
{
  if (!m_item) {
    QMessageBox::information(this, "Density Plot Options", "No solution loaded.");
    return;
  }
  // Triggering this via the Density Plot action itself (part of an
  // exclusive QActionGroup) already auto-checked it before this slot
  // runs -- remember whether Contour was actually active so a Cancel
  // below can put the checkmark back where it belongs instead of leaving
  // Density checked despite nothing having actually switched.
  bool wasContour = m_item->plotMode() == MeshSolutionItem::PlotMode::Contour;
  DensityPlotOptionsDialog dlg(m_item, m_view->legendVisible(), m_frequency != 0, this);
  if (dlg.exec() == QDialog::Accepted) {
    m_item->setPlotMode(MeshSolutionItem::PlotMode::Density);
    if (m_densityAction)
      m_densityAction->setChecked(true);
    m_view->setLegendVisible(dlg.legendVisible());
    m_view->updateAntialiasingForScale();
    m_view->refreshLegend();
  } else if (wasContour && m_contourAction) {
    m_contourAction->setChecked(true);
  }
}

void SolutionWindow::onContourOptionsTriggered()
{
  if (!m_item) {
    QMessageBox::information(this, "Contour Plot", "No solution loaded.");
    return;
  }
  // See onDensityOptionsTriggered's identical reasoning.
  bool wasDensity = m_item->plotMode() == MeshSolutionItem::PlotMode::Density;
  ContourPlotOptionsDialog dlg(m_item, m_frequency != 0, this);
  if (dlg.exec() == QDialog::Accepted) {
    m_item->setPlotMode(MeshSolutionItem::PlotMode::Contour);
    if (m_contourAction)
      m_contourAction->setChecked(true);
    m_view->updateAntialiasingForScale();
    m_view->refreshLegend();
  } else if (wasDensity && m_densityAction) {
    m_densityAction->setChecked(true);
  }
}

void SolutionWindow::onProblemInfoTriggered()
{
  if (m_currentPath.isEmpty()) {
    QMessageBox::information(this, "Problem Info", "No solution loaded.");
    return;
  }
  FemmProblem problem;
  QString error;
  // .ans shares its non-solved format's tag set for this header/property
  // section (see FemmFileIO.h) -- readFem happily parses that part and
  // silently skips the trailing [Solution] mesh data it doesn't
  // recognize, so this is a full second file read but a cheap one
  // relative to actually parsing the mesh (which is already loaded in
  // m_solution anyway).
  bool ok = FemmFileIO::readFem(m_currentPath, problem, error);
  if (!ok) {
    QMessageBox::warning(this, "Problem Info", error);
    return;
  }

  // Matches femm/FemmviewView.cpp's OnViewInfo exactly -- Title, Length
  // Units, Problem Type (+ Depth only for Planar, omitted entirely for
  // Axisymmetric), Frequency, mesh node/element counts. Classic doesn't
  // show Precision or Materials/Boundaries/Circuits counts here at all
  // (an earlier, unverified pass had added those) -- and writes this
  // straight into the Output Window rather than a popup dialog; kept as
  // a small OK dialog here (consistent with this app's other on-demand
  // info displays) but ALSO echoed to the Output Window for parity with
  // where classic actually puts it.
  QString lengthUnitsText;
  switch (problem.lengthUnits) {
  case FemmLengthUnits::Inches: lengthUnitsText = "Inches"; break;
  case FemmLengthUnits::Millimeters: lengthUnitsText = "Millimeters"; break;
  case FemmLengthUnits::Centimeters: lengthUnitsText = "Centimeters"; break;
  case FemmLengthUnits::Mils: lengthUnitsText = "Mils"; break;
  case FemmLengthUnits::Microns: lengthUnitsText = "Micrometers"; break;
  default: lengthUnitsText = "Meters"; break;
  }
  bool axisymmetric = problem.problemType == FemmCoordinateType::Axisymmetric;

  QDialog dlg(this);
  dlg.setWindowTitle("Problem Info");
  auto* form = new QFormLayout(&dlg);
  form->addRow("Title:", new QLabel(QFileInfo(m_currentPath).fileName()));
  form->addRow("Length Units:", new QLabel(lengthUnitsText));
  if (axisymmetric)
    form->addRow("Problem Type:", new QLabel("Axisymmetric Solution"));
  else
    form->addRow("Problem Type:", new QLabel(QString("2-D Planar (Depth: %1)").arg(problem.depth, 0, 'g', 6)));
  form->addRow("Frequency:", new QLabel(QString("%1 Hz").arg(problem.frequency, 0, 'g', 6)));
  form->addRow("Nodes:", new QLabel(QString::number(m_solution.nodes.size())));
  form->addRow("Elements:", new QLabel(QString::number(m_solution.elements.size())));
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  form->addRow(buttons);

  QString outputText = QString("Title: %1\nLength Units: %2\n%3\nFrequency: %4 Hz\n\n%5 Nodes\n%6 Elements")
                            .arg(QFileInfo(m_currentPath).fileName(), lengthUnitsText,
                                axisymmetric ? "Axisymmetric Solution" : QString("2-D Planar (Depth: %1)").arg(problem.depth, 0, 'g', 6))
                            .arg(problem.frequency, 0, 'g', 6)
                            .arg(m_solution.nodes.size())
                            .arg(m_solution.elements.size());
  appendOutput(outputText);
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
    // Matches femm/CircDlg.cpp's CCircDlg::OnSelchangeCircname exactly --
    // these two ratio lines are skipped entirely (not shown as inf/nan)
    // whenever the circuit's total current is zero.
    if (r.amps != std::complex<double>(0, 0)) {
      text += QString("Flux/Current = %1 Henries\nVoltage/Current = %2 Ohms\n")
                  .arg(complexToString(r.fluxLinkage / r.amps), complexToString(r.voltsDrop / r.amps));
    }
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
  QMessageBox::about(this, "About FEMMX", "<b>FEMMX (Qt)</b> -- Solution Viewer<br><br>Density/Contour plots, Point/Contour/Area analysis tools.");
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
    m_view->fitInViewSafe(m_item->boundingRect());
  m_view->updateAntialiasingForScale();
}

void SolutionWindow::onZoomWindowTriggered()
{
  m_view->startZoomWindow();
}

void SolutionWindow::onZoomWindowSelected(QRectF sceneRect)
{
  m_view->fitInViewSafe(sceneRect);
  m_view->updateAntialiasingForScale();
}

void SolutionWindow::onKbdZoomTriggered()
{
  // Matches femm/FemmviewView.cpp's OnKbdZoom (IDD_KBDZOOM) -- see
  // MainWindow::onKbdZoomTriggered's identical geometry-editor version for
  // the full comment on reusing fitInViewSafe.
  QRectF visible = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();

  QDialog dlg(this);
  dlg.setWindowTitle("Set View Bounds");
  auto* form = new QFormLayout(&dlg);
  auto* leftEdit = new QLineEdit(QString::number(visible.left(), 'g', 6));
  auto* rightEdit = new QLineEdit(QString::number(visible.right(), 'g', 6));
  auto* topEdit = new QLineEdit(QString::number(visible.bottom(), 'g', 6));
  auto* bottomEdit = new QLineEdit(QString::number(visible.top(), 'g', 6));
  for (auto* e : {leftEdit, rightEdit, topEdit, bottomEdit})
    e->setValidator(new QDoubleValidator(e));
  form->addRow("Left:", leftEdit);
  form->addRow("Right:", rightEdit);
  form->addRow("Top:", topEdit);
  form->addRow("Bottom:", bottomEdit);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  form->addRow(buttons);
  if (dlg.exec() != QDialog::Accepted)
    return;

  double x0 = leftEdit->text().toDouble();
  double x1 = rightEdit->text().toDouble();
  double y0 = topEdit->text().toDouble();
  double y1 = bottomEdit->text().toDouble();
  QRectF rect(qMin(x0, x1), qMin(y0, y1), qAbs(x1 - x0), qAbs(y1 - y0));
  m_view->fitInViewSafe(rect);
  m_view->updateAntialiasingForScale();
}

void SolutionWindow::onSetGridTriggered()
{
  // Per direct user request -- classic's own Grid Properties dialog shows
  // a bare number with no unit indication at all.
  QString label = QString("Grid Spacing (%1):").arg(lengthUnitsName(m_problemGeometry.lengthUnits));
  bool ok = false;
  double size = QInputDialog::getDouble(this, "Set Grid", label, m_scene->gridSize(), 1e-6, 1e6, 6, &ok);
  if (ok)
    m_scene->setGridSize(size);
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

void SolutionWindow::onPreferencesTriggered()
{
  bool wasDark = AppTheme::isDark();
  PreferencesDialog dlg(this);
  if (dlg.exec() == QDialog::Accepted && AppTheme::isDark() != wasDark) {
    m_scene->setBackgroundBrush(AppTheme::background());
    m_scene->update();
    refreshToolbarIcons();
  }
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
  // shared list) rather than a solved .ans/.ansx -- route it back to a
  // geometry editor window instead of trying to open it here.
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
