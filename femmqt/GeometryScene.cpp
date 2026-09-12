#define _USE_MATH_DEFINES

#include "GeometryScene.h"

#include "AppTheme.h"
#include "FemmProblem.h"
#include "FemmProblemEdit.h"
#include "IconTheme.h"
#include "MeshOverlay.h"
#include "MeshOverlayItem.h"

#include <QGraphicsDropShadowEffect>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPen>
#include <QPixmap>

#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>

namespace {


// Reduces an AngleLines dimension to the same (vertex, ray1, ray2) triple
// the 3-node Angle form uses, so every place that already knows how to
// draw or measure an angle can handle both with one extra branch instead
// of a parallel implementation.
//
// The vertex is where the two INFINITE lines cross, which is the whole
// point of this dimension type: Fusion dimensions non-touching lines
// against exactly that virtual corner. Returns false for parallel lines,
// where no such point exists and no angle dimension is meaningful.
bool angleLinesFrame(const FemmProblem& p, const FemmDimension& d,
    QPointF& vertex, QPointF& ray1, QPointF& ray2)
{
  if (d.refA < 0 || d.refA >= p.segments.size() || d.refB < 0
      || d.refB >= p.segments.size() || d.refA == d.refB)
    return false;
  const FemmSegment& s0 = p.segments[d.refA];
  const FemmSegment& s1 = p.segments[d.refB];
  const int n = p.nodes.size();
  if (s0.n0 < 0 || s0.n0 >= n || s0.n1 < 0 || s0.n1 >= n || s1.n0 < 0
      || s1.n0 >= n || s1.n1 < 0 || s1.n1 >= n)
    return false;

  QPointF a(p.nodes[s0.n0].x, p.nodes[s0.n0].y);
  QPointF b(p.nodes[s0.n1].x, p.nodes[s0.n1].y);
  QPointF c(p.nodes[s1.n0].x, p.nodes[s1.n0].y);
  QPointF e(p.nodes[s1.n1].x, p.nodes[s1.n1].y);

  const double dx0 = b.x() - a.x(), dy0 = b.y() - a.y();
  const double dx1 = e.x() - c.x(), dy1 = e.y() - c.y();
  const double denom = dx0 * dy1 - dy0 * dx1;
  const double scale = std::max(std::hypot(dx0, dy0), std::hypot(dx1, dy1));
  if (scale <= 0 || std::abs(denom) < 1e-12 * scale * scale)
    return false; // parallel (or a degenerate zero-length segment)

  const double t = ((c.x() - a.x()) * dy1 - (c.y() - a.y()) * dx1) / denom;
  vertex = QPointF(a.x() + t * dx0, a.y() + t * dy0);

  // Point each ray at whichever end of its own segment is farther from
  // the vertex, so the drawn rays lie along the real lines rather than
  // doubling back through the intersection.
  auto farther = [&vertex](QPointF p0, QPointF p1) {
    return (std::hypot(p0.x() - vertex.x(), p0.y() - vertex.y())
               >= std::hypot(p1.x() - vertex.x(), p1.y() - vertex.y()))
        ? p0
        : p1;
  };
  ray1 = farther(a, b);
  ray2 = farther(c, e);
  return true;
}

// The angle a user expects to read off two lines: the opening at the
// corner, in [0,180). Signed direction is meaningless for lines (a
// segment stored end-for-end is the same line), which is also why
// residualAngleLines works modulo 180.
double angleLinesDegrees(QPointF vertex, QPointF ray1, QPointF ray2)
{
  double a1 = std::atan2(ray1.y() - vertex.y(), ray1.x() - vertex.x());
  double a2 = std::atan2(ray2.y() - vertex.y(), ray2.x() - vertex.x());
  double diff = a2 - a1;
  while (diff > M_PI)
    diff -= 2 * M_PI;
  while (diff <= -M_PI)
    diff += 2 * M_PI;
  double deg = diff * 180.0 / M_PI;
  if (deg < 0)
    deg += 180.0;
  return deg;
}


constexpr int KindKey = 0;
constexpr int IndexKey = 1;

// Fixed screen-pixel sizes for node/block-label handles -- see the
// ItemIgnoresTransformations comment in NodeItem below for why these are
// pixel constants rather than a world-space fraction of the model's
// bounding box.
constexpr double kNodeHandlePixelRadius = 5.0;
constexpr double kBlockLabelPixelRadius = 6.0;

// Scene-units-per-screen-pixel scale factor for whatever view this item is
// currently attached to -- shared by widenedHitShape (segment/arc hit-test
// tolerance) and NodeItem/BlockLabelItem's refreshFixedSize() below, both
// of which need to convert a fixed SCREEN-pixel distance into scene units
// regardless of zoom level or the model's real-world length units.
double viewScaleFor(const QGraphicsItem* item)
{
  double scale = 1.0;
  if (item->scene() && !item->scene()->views().isEmpty()) {
    const QTransform t = item->scene()->views().first()->transform();
    scale = std::hypot(t.m11(), t.m12());
  }
  return scale > 0.0 ? scale : 1.0;
}

// Plain QGraphicsItem subclass (not QObject-based -- QGraphicsItem isn't
// QObject unless you go through QGraphicsObject, and this needs neither
// signals nor slots), so it needs no moc processing and can live entirely
// in this .cpp file. Overrides itemChange to push a drag back into the
// live FemmProblem and ask the scene to keep connected segments/arcs in
// sync, instead of requiring a full rebuild() per drag frame.
class NodeItem : public QGraphicsEllipseItem {
  public:
  NodeItem(int nodeIndex, FemmProblem* problem, GeometryScene* scene, const QRectF& rect)
      : QGraphicsEllipseItem(rect)
      , m_nodeIndex(nodeIndex)
      , m_problem(problem)
      , m_scene(scene)
  {
    setFlag(QGraphicsItem::ItemIsMovable);
    setFlag(QGraphicsItem::ItemIsSelectable);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges);
    // Nodes must hit-test above segments/arcs regardless of add order --
    // without this, clicking a node that already has a segment attached
    // can hit the segment's line (added after the node, so higher in
    // z-order by default) instead of the node itself, silently breaking
    // the Add Segment tool's "click node A, then node B" flow for any
    // node that's already connected to something (confirmed directly:
    // automated add-segment clicks worked for the first pair of nodes
    // but silently did nothing for subsequent already-connected ones).
    setZValue(1.0);
    setData(KindKey, static_cast<int>(FemmItemKind::Node));
    setData(IndexKey, nodeIndex);
  }

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-26: was
  // ItemIgnoresTransformations (kept a constant on-screen size regardless
  // of view zoom -- without SOME such mechanism, a node's world-space
  // radius, a small fraction of the model's bounding box, can render as
  // 1-2 screen pixels at typical zoom, making nodes nearly impossible to
  // click precisely, confirmed directly earlier this session). That flag's
  // interaction with QGraphicsView's MinimalViewportUpdate dirty-rect
  // tracking turned out to be unreliable during rapid position changes,
  // leaving stale drag trails no reasonable amount of manual invalidate()
  // patching could fully clear (see the git history for the extent of
  // that investigation). This achieves the same constant-screen-size
  // effect through NORMAL scene-space geometry instead -- the same
  // "fixed pixel tolerance / current view scale" technique already
  // proven for segment/arc hit-testing (see widenedHitShape) -- so Qt's
  // standard, correctly-functioning per-item dirty tracking applies with
  // no special-casing at all. Called once right after constructing/
  // adding the item (needs item->scene() to resolve the view, so can't
  // run any earlier) and again whenever GeometryView's transform changes
  // (see refreshFixedPixelItemSizes()).
  void refreshFixedSize()
  {
    double r = kNodeHandlePixelRadius / viewScaleFor(this);
    setRect(-r, -r, 2 * r, 2 * r);
  }

  protected:
  QVariant itemChange(GraphicsItemChange change, const QVariant& value) override
  {
    // Fires before the position is actually applied -- snapping here (by
    // returning a modified value) affects the drag itself, not just where
    // it lands, matching how classic FEMM applies grid snap to "the
    // current mouse position" for every interaction, not just placement.
    // Skipped while the item is being given its initial position (creation
    // in addNodeItem(), or every node during rebuild()) -- see
    // isSettingInitialItemPosition()'s comment: that position already came
    // from m_problem and must be reproduced exactly, not silently shifted
    // to the nearest grid point.
    if (change == ItemPositionChange && m_scene && m_scene->snapToGrid() && !m_scene->isSettingInitialItemPosition()) {
      QPointF center = value.toPointF() + rect().center();
      return m_scene->snapPoint(center) - rect().center();
    }
    if (change == ItemPositionHasChanged && m_problem && m_nodeIndex >= 0 && m_nodeIndex < m_problem->nodes.size()
        && m_scene && !m_scene->isSettingInitialItemPosition()) {
      // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-25:
      // per user report ("undo does not work for all the drawing
      // operations") -- this mutated m_problem on every single drag frame
      // with no snapshot ever taken, so Undo silently couldn't restore a
      // dragged node's pre-drag position. snapshotOnceForDrag() only
      // actually emits on the FIRST call within one press-drag-release
      // gesture (see its own comment), so this doesn't flood the 20-step
      // undo stack with near-duplicate frames from a single drag.
      //
      // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-26:
      // added the isSettingInitialItemPosition() guard above -- without
      // it, addNodeItem()'s own setPos() call (giving a freshly created
      // node its initial position) fired this exact same branch, pushing a
      // second, spurious undo snapshot on top of the one handleToolClick()
      // already took, and rebuild() (file open/New/every Undo) did the
      // same once per node. See that method's comment for the full story.
      m_scene->snapshotOnceForDrag();
      QPointF center = value.toPointF() + rect().center();
      m_problem->nodes[m_nodeIndex].x = center.x();
      m_problem->nodes[m_nodeIndex].y = center.y();
      m_scene->onNodeMoved(m_nodeIndex);
    }
    return QGraphicsEllipseItem::itemChange(change, value);
  }

  private:
  int m_nodeIndex;
  FemmProblem* m_problem;
  GeometryScene* m_scene;
};

// QGraphicsPathItem's default shape() (used for hit-testing, including
// double-click dispatch -- see GeometryScene::mouseDoubleClickEvent) is
// just a thin stroked outline of its path -- fine for the segment/arc
// lines, but the block-label crosshair is two 1px-wide strokes crossing
// at a point, which is a much less forgiving target than a node's filled
// circle. Confirmed directly: double-clicking a freshly-placed label at
// its exact creation coordinates still missed. Overriding shape() to a
// filled circle (like NodeItem's hit area) fixes it without changing how
// the crosshair itself is painted (paint() still just draws path()).
class BlockLabelItem : public QGraphicsPathItem {
  public:
  BlockLabelItem(const QPainterPath& path, qreal hitRadius, int labelIndex, FemmProblem* problem, GeometryScene* scene)
      : QGraphicsPathItem(path)
      , m_hitRadius(hitRadius)
      , m_labelIndex(labelIndex)
      , m_problem(problem)
      , m_scene(scene)
  {
  }

  QPainterPath shape() const override
  {
    QPainterPath p;
    p.addEllipse(QPointF(0, 0), m_hitRadius, m_hitRadius);
    return p;
  }

  // See NodeItem::refreshFixedSize()'s own comment -- same fix (constant
  // on-screen size via scene-space geometry sized from the current view
  // scale, instead of ItemIgnoresTransformations), applied to the
  // block-label crosshair marker instead of a node's filled circle.
  // Rebuilds both the hit-test radius and the painted crosshair itself,
  // since both were previously sized in a fixed local coordinate space
  // that ItemIgnoresTransformations made behave like a fixed pixel size.
  void refreshFixedSize()
  {
    m_hitRadius = kBlockLabelPixelRadius / viewScaleFor(this);
    QPainterPath path;
    path.moveTo(-m_hitRadius, 0);
    path.lineTo(m_hitRadius, 0);
    path.moveTo(0, -m_hitRadius);
    path.lineTo(0, m_hitRadius);
    setPath(path);
  }

  protected:
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-25: was
  // missing entirely -- this item was ItemIsMovable but never synced a
  // drag back into m_problem->blockLabels at all (unlike NodeItem, which
  // always has), meaning a dragged label's new position lived ONLY on the
  // QGraphicsItem: it displayed correctly right up until the next
  // rebuild() or save, at which point it would silently revert to (or
  // save) the OLD position, since nothing ever read it back off the item.
  // Same pattern as NodeItem's own itemChange now: grid-snap the drag
  // itself, sync the committed position back to m_problem, and take one
  // undo snapshot per drag gesture (not per frame).
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-26:
  // added isSettingInitialItemPosition() guards -- same bug as NodeItem's
  // itemChange (see its comment): addBlockLabelItem()'s own setPos() call
  // fired this exact branch too, so every Add Block Label click pushed a
  // spurious second undo snapshot on top of handleToolClick()'s real one,
  // and rebuild() did the same once per label.
  QVariant itemChange(GraphicsItemChange change, const QVariant& value) override
  {
    if (change == ItemPositionChange && m_scene && m_scene->snapToGrid() && !m_scene->isSettingInitialItemPosition())
      return m_scene->snapPoint(value.toPointF());
    if (change == ItemPositionHasChanged && m_problem && m_labelIndex >= 0 && m_labelIndex < m_problem->blockLabels.size()
        && m_scene && !m_scene->isSettingInitialItemPosition()) {
      m_scene->snapshotOnceForDrag();
      QPointF pos = value.toPointF();
      m_problem->blockLabels[m_labelIndex].x = pos.x();
      m_problem->blockLabels[m_labelIndex].y = pos.y();
      m_scene->onBlockLabelMoved(m_labelIndex);
    }
    return QGraphicsPathItem::itemChange(change, value);
  }

  private:
  qreal m_hitRadius;
  int m_labelIndex;
  FemmProblem* m_problem;
  GeometryScene* m_scene;
};

// Computes the same circle (center, radius, start angle) as
// CFemmeDoc::GetCircle (femm/FemmeDoc.cpp) -- shared by both the initial
// build and by live geometry updates when a node is dragged.
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
  // Qt's QPainterPath::arcTo measures angles with the y-axis effectively
  // negated relative to plain math atan2/cos/sin (its documented example:
  // 0 deg = 3 o'clock, 90 deg = 12 o'clock, even though scene y increases
  // downward) -- so the angle that makes Qt's own point-at-angle formula
  // reproduce (x0, y0) needs a negated y term here, not a plain atan2.
  // Confirmed empirically: without this, a vertical 180-degree arc (e.g.
  // an ABC shell circle's node pair) drew its curve correctly but ALSO
  // grew a spurious straight bridge from the moveTo() point to the
  // (wrong, y-mirrored) point Qt itself considered the arc's start.
  startAngleDeg = std::atan2(-(y0 - cy), x0 - cx) * 180.0 / M_PI;
  return true;
}

// Fixed screen-pixel hit-test tolerance for segments/arcs -- see
// widenedHitShape's comment.
constexpr double kLineHitPixelTolerance = 8.0;

// QGraphicsLineItem/QGraphicsPathItem's default shape() (used for
// hit-testing, including click-to-select) strokes the path using the
// item's own pen width. That's fine for painting -- segments/arcs use a
// cosmetic 0-width hairline (see addSegmentItem's comment) -- but murder
// for hit-testing: a click has to land almost exactly on the
// mathematical line. This widens the hit-test-only shape to a fixed
// SCREEN-pixel tolerance by consulting the attached view's current
// scale, so segments/arcs stay just as easy to click near regardless of
// zoom level or the model's real-world length units.
QPainterPath widenedHitShape(const QPainterPath& path, const QGraphicsItem* item)
{
  QPainterPathStroker stroker;
  stroker.setWidth(kLineHitPixelTolerance / viewScaleFor(item));
  return stroker.createStroke(path);
}

// QGraphicsLineItem subclass for segments -- see widenedHitShape's
// comment for the hit-testing half of this class. Also thickens its own
// paint() while selected: the drop-shadow selection effect
// (GeometryScene::onSelectionChanged) blurs whatever the item actually
// painted, and a 0-width cosmetic hairline leaves it almost nothing to
// blur -- confirmed directly, nodes (filled circles) got an obvious glow
// on selection while segments/arcs barely showed anything. Drawing a
// visibly thicker stroke only while selected gives the same shadow
// effect real content to work with, so lines get a highlight comparable
// to nodes' instead of a near-invisible one.
class SegmentItem : public QGraphicsLineItem {
  public:
  using QGraphicsLineItem::QGraphicsLineItem;

  QPainterPath shape() const override
  {
    QPainterPath p;
    p.moveTo(line().p1());
    p.lineTo(line().p2());
    return widenedHitShape(p, this);
  }

  protected:
  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override
  {
    if (!isSelected()) {
      QGraphicsLineItem::paint(painter, option, widget);
      return;
    }
    QPen p = pen();
    p.setWidth(3); // cosmetic pen -- a fixed 3 screen pixels regardless of zoom
    painter->setPen(p);
    painter->drawLine(line());
  }
};

// QGraphicsPathItem subclass for arcs -- same reasoning as SegmentItem
// above, just over path() instead of line().
class ArcItem : public QGraphicsPathItem {
  public:
  using QGraphicsPathItem::QGraphicsPathItem;

  QPainterPath shape() const override
  {
    return widenedHitShape(path(), this);
  }

  protected:
  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override
  {
    if (!isSelected()) {
      QGraphicsPathItem::paint(painter, option, widget);
      return;
    }
    QPen p = pen();
    p.setWidth(3);
    painter->setPen(p);
    painter->drawPath(path());
  }
};

// Modified by Claude (Anthropic), noreply@anthropic.com: open two-stroke
// "caret" arrowhead -- not a filled triangle, since DimensionItem paints
// with a pen only (no brush is ever set on it; see its class comment for
// why introducing one is riskier than it looks). Appends to `path` at
// `tip`, with its two wings pointing back along `backDirUnit` (a UNIT
// vector pointing away from the line/arc the arrowhead terminates -- from
// the tip back toward the rest of the dimension). Shared by all 3
// DimensionType cases in updateGeometry() below.
void addArrowhead(QPainterPath& path, QPointF tip, QPointF backDirUnit, double len)
{
  constexpr double kHalfAngle = 8.0 * M_PI / 180.0; // ~16 degree included angle -- narrow/slender, per direct user request ("smaller arrows")
  double c = std::cos(kHalfAngle), s = std::sin(kHalfAngle);
  QPointF back = backDirUnit * len;
  QPointF wing1(back.x() * c - back.y() * s, back.x() * s + back.y() * c);
  QPointF wing2(back.x() * c + back.y() * s, -back.x() * s + back.y() * c);
  path.moveTo(tip);
  path.lineTo(tip + wing1);
  path.moveTo(tip);
  path.lineTo(tip + wing2);
}

// Modified by Claude (Anthropic), noreply@anthropic.com: CAD-style
// dimension annotation -- see FemmProblem.h's FemmDimension comment.
// Same "own QGraphicsItem subclass, geometry recomputed on demand rather
// than every paint" shape as SegmentItem/ArcItem above, via
// updateGeometry() (called at creation and from GeometryScene::
// onNodeMoved for every referenced node, mirroring
// updateSegmentItemGeometry/updateArcItemGeometry's role exactly) rather
// than a free function, since it also needs to reposition its own child
// text label -- keeping both in one method avoids two places needing to
// agree on the same math.
//
// Modified by Claude (Anthropic), noreply@anthropic.com: rendering
// reworked to match Fusion 360's own dimension style, per direct user
// request (a Fusion 360 screenshot supplied as reference) -- was a bare
// offset line with no arrowheads/gaps and, for Angle, a straight-line
// wedge rather than a true arc (see the removed comment below for why
// that was originally deferred). Distance now draws extension lines with
// a small gap near the measured points and a small overshoot past the
// dimension line (both scaled off the user's own placement offset, since
// this item draws entirely in scene units -- see NodeItem::refreshFixedSize's
// comment for the fixed-SCREEN-pixel technique used elsewhere in this
// file for UI chrome like node handles, deliberately NOT used here: a
// dimension line is drawing content, not UI chrome, so it should scale
// with the model like any other geometry), plus open caret arrowheads at both
// ends (addArrowhead() above). Radius adds one arrowhead where the
// leader touches the arc. Angle now draws a true arc (solving the
// QPainterPath::arcTo sign-convention question the removed comment
// flagged -- same y-flip compensation as arcGeometry()/
// updateArcItemGeometry's own arcTo() call, see the inline comment at
// its call site below) with an arrowhead at each end, tangent to the
// arc.
// Modified by Claude (Anthropic), noreply@anthropic.com: extracted out of
// DimensionItem::updateGeometry() so the Smart Dimension tool's live
// placement preview (see updateSmartDimensionPreview()) can build the
// exact same path a COMMITTED dimension would render, from a temporary,
// not-yet-pushed-into-p.dimensions FemmDimension -- a plain mechanical
// extraction, no behavior change. Distance/HorizontalDistance/
// VerticalDistance share this case but do NOT render identically (see the
// per-type dimA/dimB derivation inside it): Horizontal/Vertical lock the
// dimension line to a single shared Y/X, Aligned (Distance) keeps it
// parallel to the measured segment -- matching how the solver actually
// constrains each of them (see ConstraintSolver.cpp), not just their
// value.
QPainterPath buildDimensionPath(const FemmProblem& p, const FemmDimension& d, QPointF& textPos, QString& text)
{
  const QVector<FemmNode>& nodes = p.nodes;
  QPainterPath path;

  switch (d.type) {
  case DimensionType::Distance:
  case DimensionType::HorizontalDistance:
  case DimensionType::VerticalDistance: {
    if (d.refA < 0 || d.refA >= nodes.size() || d.refB < 0 || d.refB >= nodes.size())
      break;
    QPointF a(nodes[d.refA].x, nodes[d.refA].y);
    QPointF b(nodes[d.refB].x, nodes[d.refB].y);
    QPointF offset(d.labelOffsetX, d.labelOffsetY);
    QPointF mid = (a + b) / 2.0;

    // Modified by Claude (Anthropic), noreply@anthropic.com: REAL rendering
    // bug found per direct user request/screenshot ("does not seem nice...
    // same way as in fusion 360") -- dimA/dimB used to be a/b translated by
    // the SAME raw offset vector, which makes dimB-dimA ALWAYS exactly
    // b-a: the dimension line was silently parallel to whatever the
    // measured segment's own angle happened to be, for EVERY type,
    // Horizontal and Vertical included. That's only correct for Aligned.
    // Confirmed against both a real Fusion 360 screenshot (solid axis-
    // locked dimension lines) and this session's own Fusion 360 reference
    // doc (Section 6: a Horizontal dimension spans purely horizontally
    // under an inclined line, "does not require the points to lie on the
    // same horizontal line"). Fixed by deriving dimA/dimB per type instead
    // of applying one shared offset vector to both points: Horizontal
    // locks the dimension line to a single Y (extension lines vertical,
    // independently as long or short as each point needs); Vertical locks
    // it to a single X (extension lines horizontal); Aligned keeps the
    // dimension line parallel to the segment, using only the PERPENDICULAR
    // component of the raw cursor offset -- any along-the-segment
    // component would just slide both extension lines sideways together in
    // lockstep, which changes nothing visually and isn't how Fusion 360's
    // own aligned dimension behaves (cursor there controls offset
    // DISTANCE, not free 2D placement).
    QPointF dimA, dimB;       // where each extension line meets the dimension line
    QPointF extDirA, extDirB; // unit direction FROM each point TOWARD the dimension line
    if (d.type == DimensionType::HorizontalDistance) {
      double dimY = mid.y() + offset.y();
      dimA = QPointF(a.x(), dimY);
      dimB = QPointF(b.x(), dimY);
      extDirA = QPointF(0, dimY >= a.y() ? 1.0 : -1.0);
      extDirB = QPointF(0, dimY >= b.y() ? 1.0 : -1.0);
    } else if (d.type == DimensionType::VerticalDistance) {
      double dimX = mid.x() + offset.x();
      dimA = QPointF(dimX, a.y());
      dimB = QPointF(dimX, b.y());
      extDirA = QPointF(dimX >= a.x() ? 1.0 : -1.0, 0);
      extDirB = QPointF(dimX >= b.x() ? 1.0 : -1.0, 0);
    } else {
      QPointF segDir = b - a;
      double segLen = std::hypot(segDir.x(), segDir.y());
      QPointF normal = segLen > 1e-9 ? QPointF(-segDir.y(), segDir.x()) / segLen : QPointF(0, 1);
      double perp = offset.x() * normal.x() + offset.y() * normal.y();
      dimA = a + normal * perp;
      dimB = b + normal * perp;
      extDirA = extDirB = (perp >= 0 ? normal : -normal);
    }

    // Gap near the measured point + overshoot past the dimension line,
    // each scaled off THAT point's own extension length (which, for
    // Horizontal/Vertical, can now legitimately differ between the two
    // points) rather than a single shared offset magnitude -- naturally
    // degrades to "no visible extension line" if the dimension line
    // happens to pass exactly through a point, which is correct: there's
    // nothing to extend.
    auto drawExtension = [&](QPointF from, QPointF to, QPointF dirUnit) {
      double len = std::hypot(to.x() - from.x(), to.y() - from.y());
      if (len < 1e-9)
        return;
      double gap = len * 0.10;
      double overshoot = len * 0.15;
      path.moveTo(from + dirUnit * gap);
      path.lineTo(from + dirUnit * (len + overshoot));
    };
    drawExtension(a, dimA, extDirA);
    drawExtension(b, dimB, extDirB);

    path.moveTo(dimA);
    path.lineTo(dimB);
    double lineLen = std::hypot(dimB.x() - dimA.x(), dimB.y() - dimA.y());
    if (lineLen > 1e-9) {
      QPointF along = (dimB - dimA) / lineLen;
      double arrowLen = lineLen * 0.05;
      addArrowhead(path, dimA, along, arrowLen);
      addArrowhead(path, dimB, -along, arrowLen);
    }
    textPos = (dimA + dimB) / 2.0;
    text = QString::number(d.value, 'g', 6);
    break;
  }
  case DimensionType::Radius: {
    if (d.refA < 0 || d.refA >= p.arcSegments.size())
      break;
    std::complex<double> c;
    double r = 0;
    if (!FemmProblemEdit::circleFromArc(p, p.arcSegments[d.refA], c, r))
      break;
    QPointF center(c.real(), c.imag());
    QPointF dir = (d.labelOffsetX != 0 || d.labelOffsetY != 0) ? QPointF(d.labelOffsetX, d.labelOffsetY) : QPointF(1, 0);
    double dirLen = std::hypot(dir.x(), dir.y());
    if (dirLen <= 0)
      break;
    dir /= dirLen;
    QPointF edge = center + dir * r;
    path.moveTo(center);
    path.lineTo(edge);
    if (r > 1e-9)
      addArrowhead(path, edge, -dir, r * 0.08);
    textPos = edge;
    text = QString("R%1").arg(d.value, 0, 'g', 6);
    break;
  }
  case DimensionType::AngleLines:
  case DimensionType::Angle: {
    QPointF v, p1, p2;
    if (d.type == DimensionType::AngleLines) {
      if (!angleLinesFrame(p, d, v, p1, p2))
        break;
    } else {
      if (d.refA < 0 || d.refA >= nodes.size() || d.refB < 0 || d.refB >= nodes.size() || d.refC < 0 || d.refC >= nodes.size())
        break;
      v = QPointF(nodes[d.refA].x, nodes[d.refA].y);
      p1 = QPointF(nodes[d.refB].x, nodes[d.refB].y);
      p2 = QPointF(nodes[d.refC].x, nodes[d.refC].y);
    }
    double r = std::min(std::hypot(p1.x() - v.x(), p1.y() - v.y()), std::hypot(p2.x() - v.x(), p2.y() - v.y())) * 0.5;
    if (r <= 0)
      break;
    double a1 = std::atan2(p1.y() - v.y(), p1.x() - v.x());
    double a2 = std::atan2(p2.y() - v.y(), p2.x() - v.x());
    // Same signed-shorter-angle wrap as the AddDimensionAngle tool uses
    // to measure the initial value (see handleToolClick) -- reusing the
    // identical formula here means the rendered arc always sweeps the
    // same direction/magnitude the value itself represents.
    double diff = a2 - a1;
    while (diff > M_PI)
      diff -= 2 * M_PI;
    while (diff <= -M_PI)
      diff += 2 * M_PI;
    QPointF e1 = v + r * QPointF(std::cos(a1), std::sin(a1));
    QPointF e2 = v + r * QPointF(std::cos(a1 + diff), std::sin(a1 + diff));
    // True arc, not a straight-line wedge -- same y-flip compensation as
    // arcGeometry()/updateArcItemGeometry's own arcTo() call (see
    // arcGeometry's comment for the full explanation): Qt's arcTo angle
    // = atan2(-dy, dx), and its sweep direction is the negation of a
    // plain-math CCW sweep.
    double qtStartDeg = std::atan2(-(e1.y() - v.y()), e1.x() - v.x()) * 180.0 / M_PI;
    double qtSweepDeg = -diff * 180.0 / M_PI;
    path.moveTo(e1);
    path.arcTo(v.x() - r, v.y() - r, 2 * r, 2 * r, qtStartDeg, qtSweepDeg);

    // Arrowhead "back" direction at a point on the arc, tangent to it:
    // the plain-math travel direction along the arc at angle t is
    // sgn*(-sin t, cos t) (sgn = direction of travel, from diff's
    // sign); the arrowhead's wings point backward from that, i.e. the
    // negation, at both ends (same formula works for the start AND end
    // point -- see addArrowhead's own comment on what "back" means).
    double sgn = diff >= 0 ? 1.0 : -1.0;
    double arrowLen = r * 0.08;
    addArrowhead(path, e1, QPointF(sgn * std::sin(a1), -sgn * std::cos(a1)), arrowLen);
    addArrowhead(path, e2, QPointF(sgn * std::sin(a1 + diff), -sgn * std::cos(a1 + diff)), arrowLen);

    double midAngle = a1 + diff / 2.0;
    textPos = v + r * 1.3 * QPointF(std::cos(midAngle), std::sin(midAngle));
    text = QString("%1 deg").arg(d.value, 0, 'g', 6);
    break;
  }
  }
  return path;
}

class DimensionItem : public QGraphicsPathItem {
  public:
  DimensionItem(int dimIndex, FemmProblem* problem)
      : m_dimIndex(dimIndex)
      , m_problem(problem)
  {
    setFlag(QGraphicsItem::ItemIsSelectable);
    setData(KindKey, static_cast<int>(FemmItemKind::Dimension));
    setData(IndexKey, dimIndex);
  }

  void setTextItem(QGraphicsSimpleTextItem* text) { m_text = text; }

  void updateGeometry()
  {
    if (m_dimIndex < 0 || m_dimIndex >= m_problem->dimensions.size())
      return;
    QPointF textPos;
    QString text;
    QPainterPath path = buildDimensionPath(*m_problem, m_problem->dimensions[m_dimIndex], textPos, text);
    setPath(path);
    if (m_text) {
      m_text->setText(text);
      m_text->setPos(textPos);
    }
  }

  QPainterPath shape() const override
  {
    return widenedHitShape(path(), this);
  }

  protected:
  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override
  {
    if (!isSelected()) {
      QGraphicsPathItem::paint(painter, option, widget);
      return;
    }
    QPen p = pen();
    // Modified by Claude (Anthropic), noreply@anthropic.com: was 3 (same
    // as SegmentItem/ArcItem's own selected-state width) -- per direct
    // user request ("make the dim lines thinner"), dimension lines now
    // get a lighter selected-state emphasis than real geometry.
    p.setWidth(2);
    painter->setPen(p);
    painter->drawPath(path());
  }

  private:
  int m_dimIndex;
  FemmProblem* m_problem;
  QGraphicsSimpleTextItem* m_text = nullptr;
};

// Modified by Claude (Anthropic), noreply@anthropic.com: same 9 SVGs
// MainWindow's Constraints menu/toolbar already use (see icons.qrc) --
// reused here rather than drawn again, so a glyph always matches its
// menu/toolbar icon.
QString constraintIconPath(ConstraintType type)
{
  switch (type) {
  case ConstraintType::Coincident: return ":/icons/constraint_coincident.svg";
  case ConstraintType::Horizontal: return ":/icons/constraint_horizontal.svg";
  case ConstraintType::Vertical: return ":/icons/constraint_vertical.svg";
  case ConstraintType::Parallel: return ":/icons/constraint_parallel.svg";
  case ConstraintType::Perpendicular: return ":/icons/constraint_perpendicular.svg";
  case ConstraintType::Equal: return ":/icons/constraint_equal.svg";
  case ConstraintType::Tangent: return ":/icons/constraint_tangent.svg";
  case ConstraintType::Concentric: return ":/icons/constraint_concentric.svg";
  case ConstraintType::Symmetric: return ":/icons/constraint_symmetric.svg";
  }
  return QString();
}

constexpr double kConstraintGlyphPixelRadius = 9.0;

// Modified by Claude (Anthropic), noreply@anthropic.com: small on-canvas
// icon marking a constraint's location, matching Fusion 360's own
// on-geometry relation markers -- per direct user request ("symbols
// indicating constraints than I can click and remove"). Positioned at
// ConstraintSolver::anchorPoint() (the average of the constraint's touched
// nodes, the same node set the solver itself uses -- see that function's
// header comment), so it can never drift out of sync with what the
// constraint actually references. Uses the same fixed-screen-pixel-size
// technique as NodeItem/BlockLabelItem (a raster QPixmap stretched to fill
// a boundingRect sized from the current view scale, refreshed by
// refreshFixedSize()) rather than ItemIgnoresTransformations, for the same
// dirty-rect-tracking reasons documented on NodeItem::refreshFixedSize().
class ConstraintGlyphItem : public QGraphicsItem {
  public:
  ConstraintGlyphItem(int constraintIndex, FemmProblem* problem)
      : m_constraintIndex(constraintIndex)
      , m_problem(problem)
  {
    setFlag(QGraphicsItem::ItemIsSelectable);
    setData(KindKey, static_cast<int>(FemmItemKind::Constraint));
    setData(IndexKey, constraintIndex);
    // Above segments/arcs/dimensions (z 0/0/0) so a glyph is always
    // clickable even when it lands on top of the geometry it annotates --
    // same reasoning as NodeItem's own z-value.
    setZValue(2.0);
    if (constraintIndex >= 0 && constraintIndex < problem->constraints.size()) {
      QString path = constraintIconPath(problem->constraints[constraintIndex].type);
      if (!path.isEmpty())
        m_pixmap = IconTheme::themedToolIcon(path).pixmap(32, 32);
    }
  }

  void refreshFixedSize()
  {
    prepareGeometryChange();
    m_halfSize = kConstraintGlyphPixelRadius / viewScaleFor(this);
  }

  QRectF boundingRect() const override
  {
    return QRectF(-m_halfSize, -m_halfSize, 2 * m_halfSize, 2 * m_halfSize);
  }

  QPainterPath shape() const override
  {
    QPainterPath p;
    p.addEllipse(boundingRect());
    return p;
  }

  protected:
  void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override
  {
    QRectF r = boundingRect();
    painter->setPen(Qt::NoPen);
    painter->setBrush(isSelected() ? AppTheme::selectedColor() : AppTheme::background());
    painter->drawEllipse(r);
    if (!m_pixmap.isNull())
      painter->drawPixmap(r, m_pixmap, QRectF(m_pixmap.rect()));
  }

  private:
  int m_constraintIndex;
  FemmProblem* m_problem;
  QPixmap m_pixmap;
  double m_halfSize = kConstraintGlyphPixelRadius;
};

} // namespace

GeometryScene::GeometryScene(QObject* parent)
    : QGraphicsScene(parent)
{
  // DimensionType is only forward-declared in the header, so this can't
  // be a member-initializer default there.
  m_smartDimType = DimensionType::Distance;
  // Fixed, generous scene rect -- without an explicit one, QGraphicsScene
  // computes it from the current items' bounding rect and grows/shifts it
  // as items are added, which silently pans/rescrolls the attached
  // QGraphicsView's viewport. That breaks the mapping from a screen pixel
  // to a scene position between edits: confirmed directly this session --
  // clicking the same screen pixel twice in a row (once right after
  // adding a node, once after a later edit) resolved to two different
  // scene coordinates, several dozen world units apart, causing the
  // second click to miss the node it was aimed at entirely. A problem's
  // real geometry is always tiny compared to this range for any
  // LengthUnits this app supports, so it's not a meaningful limit.
  setSceneRect(-1.0e6, -1.0e6, 2.0e6, 2.0e6);
  setBackgroundBrush(AppTheme::background());
  connect(this, &QGraphicsScene::selectionChanged, this, &GeometryScene::onSelectionChanged);
}

void GeometryScene::onSelectionChanged()
{
  // Remove the effect from anything that's no longer selected first -- a
  // QGraphicsEffect is owned by its item (setGraphicsEffect(nullptr) is
  // how you take it off, not selectedItems() diffing against itself).
  // m_shadowedItems is guaranteed to hold only still-alive items here --
  // rebuild() clears it explicitly before its clear() call, specifically
  // so this loop never has to guess whether a pointer survived a bulk
  // item deletion.
  for (QGraphicsItem* item : std::as_const(m_shadowedItems))
    item->setGraphicsEffect(nullptr);
  m_shadowedItems.clear();

  for (QGraphicsItem* item : selectedItems()) {
    auto* effect = new QGraphicsDropShadowEffect;
    effect->setColor(AppTheme::selectedColor());
    effect->setBlurRadius(12);
    effect->setOffset(0, 0);
    item->setGraphicsEffect(effect);
    m_shadowedItems.push_back(item);
  }

  // The view's default MinimalViewportUpdate mode only repaints the exact
  // old/new bounding rects Qt tracked for a change. A QGraphicsDropShadowEffect
  // blurs beyond its item's own bounding rect, and attaching/detaching one
  // (as happens here every time selection flickers, e.g. during a drag) can
  // leave a stale, never-repainted patch of the *previous* frame on screen --
  // confirmed directly: a grey box with a faint red glow was left behind
  // after a node drag and survived even after every edit was undone, proving
  // it was a leftover paint artifact, not real scene content. A full-scene
  // update is cheap here since this only runs on selection changes, not
  // every frame.
  update();
}

void GeometryScene::setProblem(FemmProblem* problem)
{
  m_problem = problem;
  rebuild();
}

QRectF GeometryScene::computeProblemBounds() const
{
  if (!m_problem)
    return QRectF();

  bool first = true;
  double xmin = 0, xmax = 0, ymin = 0, ymax = 0;
  auto expand = [&](double x, double y) {
    if (first) {
      xmin = xmax = x;
      ymin = ymax = y;
      first = false;
    } else {
      xmin = std::min(xmin, x);
      xmax = std::max(xmax, x);
      ymin = std::min(ymin, y);
      ymax = std::max(ymax, y);
    }
  };

  for (const FemmNode& n : m_problem->nodes)
    expand(n.x, n.y);
  for (const FemmBlockLabel& b : m_problem->blockLabels)
    expand(b.x, b.y);
  for (const FemmArcSegment& a : m_problem->arcSegments) {
    if (a.n0 < 0 || a.n0 >= m_problem->nodes.size() || a.n1 < 0 || a.n1 >= m_problem->nodes.size())
      continue;
    const FemmNode& n0 = m_problem->nodes[a.n0];
    const FemmNode& n1 = m_problem->nodes[a.n1];
    double cx, cy, R, startAngleDeg;
    if (arcGeometry(n0.x, n0.y, n1.x, n1.y, a.arcLength, cx, cy, R, startAngleDeg)) {
      // Conservative: expands to the arc's full circle rather than just
      // its actual swept portion (which would need the exact start/end
      // angle range) -- a safe superset, and the difference only matters
      // for a short arc cut from a very large circle, not a realistic
      // case for this app's models.
      expand(cx - R, cy - R);
      expand(cx + R, cy + R);
    } else {
      // Degenerate arc (see arcGeometry's own early-return cases) --
      // still make sure its endpoints themselves count.
      expand(n0.x, n0.y);
      expand(n1.x, n1.y);
    }
  }

  if (first)
    return QRectF();
  return QRectF(QPointF(xmin, ymin), QPointF(xmax, ymax));
}

void GeometryScene::rebuild()
{
  // Drop these BEFORE clear() -- clear() deletes every item directly and
  // doesn't reliably fire selectionChanged() per item on the way out, so
  // onSelectionChanged()'s own cleanup can't be trusted to run first;
  // leaving stale pointers in m_shadowedItems here would otherwise mean
  // a future selection change dereferences already-deleted items.
  m_shadowedItems.clear();
  clear(); // deletes every item, including m_zoomWindowRectItem if present
  m_nodeItems.clear();
  m_segmentItemsByNode.clear();
  m_arcItemsByNode.clear();
  m_blockNameItems.clear();
  m_blockLabelItems.clear();
  m_dimensionItems.clear();
  m_dimensionItemsByNode.clear();
  m_constraintItems.clear();
  m_constraintItemsByNode.clear();
  m_zoomWindowRectItem = nullptr;
  // Modified by Claude (Anthropic), noreply@anthropic.com: same
  // already-deleted-by-clear() reasoning as m_zoomWindowRectItem right
  // above, but a REAL (not just theoretical) risk for this one
  // specifically: unlike the drag-only rubber-band previews
  // (m_selectCircleItem etc., only ever alive between a mousePress and
  // its OWN mouseRelease, a window nothing else can interrupt since the
  // OS holds mouse capture the whole time), a Smart Dimension preview
  // stays alive across multiple discrete clicks while the mouse moves
  // FREELY with no button held -- during which the user genuinely can
  // trigger Undo (Ctrl+Z) or another rebuild()-causing action, which
  // would otherwise leave these two pointers dangling after clear().
  m_smartDimPreviewItem = nullptr;
  m_smartDimPreviewText = nullptr;
  m_smartDimAwaitingPlacement = false;
  m_smartDimTwoPointMode = false;
  m_smartDimAngleEligible = false;
  m_smartDimRefA = m_smartDimRefB = m_smartDimRefC = -1;
  // clear() above already deleted this along with everything else -- an
  // edit invalidates any previous mesh anyway (matches classic FEMM's own
  // MeshUpToDate flag being cleared on any geometry change), so there's no
  // reason to try to preserve/re-add it here. m_mesh reset too (a
  // multi-million-element mesh is real memory not worth holding onto
  // past the point it's actually displayable).
  m_meshOverlayItem = nullptr;
  m_mesh = MeshOverlay();
  m_pendingNode = -1;

  if (m_problem) {
    for (int i = 0; i < m_problem->segments.size(); i++)
      addSegmentItem(i);
    for (int i = 0; i < m_problem->arcSegments.size(); i++)
      addArcItem(i);
    for (int i = 0; i < m_problem->nodes.size(); i++)
      addNodeItem(i);
    for (int i = 0; i < m_problem->blockLabels.size(); i++)
      addBlockLabelItem(i);
    // Added last so dimension annotations render (and hit-test) on top
    // of the geometry they measure.
    for (int i = 0; i < m_problem->dimensions.size(); i++)
      addDimensionItem(i);
    // Constraint glyphs added after dimensions too -- their z-value (2.0,
    // vs a dimension's default 0.0) already puts them on top for hit-
    // testing regardless of add order, but matching the same "most
    // recently added, most clickable" convention keeps this loop
    // consistent with the one above.
    for (int i = 0; i < m_problem->constraints.size(); i++)
      addConstraintItem(i);
  }

  // Cheap safety-net full-scene update() -- rebuild() only runs on
  // discrete actions (file open, New, Undo/Redo), never per-frame, so
  // this costs nothing noticeable. Originally added because node/block-
  // label markers' old ItemIgnoresTransformations flag made Qt's
  // MinimalViewportUpdate dirty-tracking unreliable on delete (a stale
  // grey square was left behind exactly where an undone node used to
  // be); kept as insurance now that those markers use normal scene-space
  // geometry instead (see NodeItem::refreshFixedSize()), since rebuild()
  // deletes and recreates EVERY item, not just markers.
  update();
}

void GeometryScene::refreshTheme()
{
  setBackgroundBrush(AppTheme::background());
  rebuild();
  update();
}

void GeometryScene::refreshFixedPixelItemSizes()
{
  // static_cast, not qgraphicsitem_cast/dynamic_cast: NodeItem/BlockLabelItem
  // are plain (non-QObject) types private to this .cpp file, and
  // m_nodeItems/m_blockLabelItems only ever hold instances of them (see
  // addNodeItem()/addBlockLabelItem(), the only places that populate
  // these hashes).
  for (QGraphicsItem* item : std::as_const(m_nodeItems))
    static_cast<NodeItem*>(item)->refreshFixedSize();
  for (QGraphicsItem* item : std::as_const(m_blockLabelItems))
    static_cast<BlockLabelItem*>(item)->refreshFixedSize();
  for (QGraphicsItem* item : std::as_const(m_constraintItems))
    static_cast<ConstraintGlyphItem*>(item)->refreshFixedSize();
}

void GeometryScene::setToolMode(GeometryToolMode mode)
{
  m_toolMode = mode;
  m_pendingNode = -1;
  m_pendingDimensionNodes.clear();
  m_pendingDimensionArc = -1;
  cancelSmartDimensionPreview();
}

// Modified by Claude (Anthropic), noreply@anthropic.com: DOF/sketch-
// health color for node `nodeIndex`, from the last ConstraintSolver::
// solve() (see setConstraintStatus()) -- an invalid QColor (isValid()
// false) means this node isn't part of any constraint/dimension, so
// callers should fall back to their normal color instead. FullyConstrained
// reuses nodeColor() itself (the default "everything's fine" look, per
// SolidWorks/FreeCAD's own convention of NOT specially highlighting
// fully-constrained geometry) rather than a 5th distinct hue.
QColor GeometryScene::constraintStatusColor(int nodeIndex) const
{
  auto it = m_constraintNodeStatus.constFind(nodeIndex);
  if (it == m_constraintNodeStatus.constEnd())
    return QColor();
  switch (it.value()) {
  case ConstraintSolver::SketchStatus::FullyConstrained:
    return AppTheme::nodeColor();
  case ConstraintSolver::SketchStatus::UnderConstrained:
    return AppTheme::segmentColor();
  case ConstraintSolver::SketchStatus::Redundant:
    return AppTheme::boundaryEdgeColor();
  case ConstraintSolver::SketchStatus::Conflicting:
    return AppTheme::selectedColor();
  }
  return QColor();
}

void GeometryScene::setConstraintStatus(const QHash<int, ConstraintSolver::SketchStatus>& nodeStatus)
{
  m_constraintNodeStatus = nodeStatus;
}

void GeometryScene::selectConstraintGlyph(int index)
{
  clearSelection();
  auto it = m_constraintItems.constFind(index);
  if (it != m_constraintItems.constEnd())
    it.value()->setSelected(true);
}

void GeometryScene::addNodeItem(int index)
{
  const FemmNode& n = m_problem->nodes[index];
  QColor statusColor = constraintStatusColor(index);
  QPen pen(statusColor.isValid() ? statusColor : AppTheme::nodeColor());
  pen.setCosmetic(true);
  // Width 0, not the QPen(color) constructor's default of 1 -- Qt treats
  // a cosmetic pen with width exactly 0 as a fast, robust "hairline" that
  // draws directly from transformed point positions, versus a cosmetic
  // pen with a nonzero (even if logically tiny) width, which goes through
  // general stroke-to-polygon tessellation (offsetting the line by half
  // its width, itself computed via the transform's inverse scale) --
  // confirmed directly as the source of lines rendering visibly thicker
  // than a device pixel at extreme zoom on a real, detailed model (see
  // MeshOverlayItem's header comment for the full investigation).
  pen.setWidth(0);
  auto* item = new NodeItem(index, m_problem, this,
      QRectF(-kNodeHandlePixelRadius, -kNodeHandlePixelRadius, 2 * kNodeHandlePixelRadius, 2 * kNodeHandlePixelRadius));
  m_settingInitialItemPosition = true;
  item->setPos(n.x, n.y);
  m_settingInitialItemPosition = false;
  item->setPen(pen);
  item->setBrush(QBrush(statusColor.isValid() ? statusColor : AppTheme::nodeColor()));
  addItem(item);
  item->refreshFixedSize(); // needs item->scene() (just set by addItem() above) to resolve the view's current scale
  m_nodeItems[index] = item;
}

void GeometryScene::addSegmentItem(int index)
{
  const FemmSegment& s = m_problem->segments[index];
  // A constraint/dimension DOF-status color, if either endpoint has one,
  // takes priority over the boundary-condition color -- more urgent,
  // actively-relevant feedback while the constraint solver is in use.
  QColor statusColor = constraintStatusColor(s.n0);
  if (!statusColor.isValid())
    statusColor = constraintStatusColor(s.n1);
  QPen pen(statusColor.isValid() ? statusColor : (s.boundaryMarker != 0 ? AppTheme::boundaryEdgeColor() : AppTheme::segmentColor()));
  pen.setCosmetic(true);
  pen.setWidth(0); // see addNodeItem's comment on width 0 vs the QPen(color) ctor's default of 1
  auto* item = new SegmentItem(QLineF());
  item->setPen(pen);
  addItem(item);
  item->setFlag(QGraphicsItem::ItemIsSelectable);
  item->setData(KindKey, static_cast<int>(FemmItemKind::Segment));
  item->setData(IndexKey, index);
  updateSegmentItemGeometry(item, index);

  m_segmentItemsByNode.insert(s.n0, item);
  m_segmentItemsByNode.insert(s.n1, item);
}

void GeometryScene::addArcItem(int index)
{
  const FemmArcSegment& a = m_problem->arcSegments[index];
  QColor statusColor = constraintStatusColor(a.n0);
  if (!statusColor.isValid())
    statusColor = constraintStatusColor(a.n1);
  QPen pen(statusColor.isValid() ? statusColor : (a.boundaryMarker != 0 ? AppTheme::boundaryEdgeColor() : AppTheme::arcColor()));
  pen.setCosmetic(true);
  pen.setWidth(0); // see addNodeItem's comment on width 0 vs the QPen(color) ctor's default of 1
  auto* item = new ArcItem(QPainterPath());
  item->setPen(pen);
  addItem(item);
  item->setFlag(QGraphicsItem::ItemIsSelectable);
  item->setData(KindKey, static_cast<int>(FemmItemKind::Arc));
  item->setData(IndexKey, index);
  updateArcItemGeometry(item, index);

  m_arcItemsByNode.insert(a.n0, item);
  m_arcItemsByNode.insert(a.n1, item);
}

void GeometryScene::addBlockLabelItem(int index)
{
  const FemmBlockLabel& b = m_problem->blockLabels[index];
  bool isHole = b.blockTypeIndex < 0;
  double r = kBlockLabelPixelRadius;
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-22: was
  // a hardcoded QColor(200,0,0), not routed through AppTheme at all --
  // the one "striking" color the user's palette pass (AppTheme.cpp) had
  // missed since it lived here, not in a named AppTheme function. Now
  // matches the label's own text color for a consistent marker+text pair.
  QPen pen(isHole ? AppTheme::holeColor() : AppTheme::blockLabelNameColor());
  pen.setCosmetic(true);
  pen.setWidth(0); // see addNodeItem's comment on width 0 vs the QPen(color) ctor's default of 1

  QPainterPath path;
  path.moveTo(-r, 0);
  path.lineTo(r, 0);
  path.moveTo(0, -r);
  path.lineTo(0, r);
  auto* item = new BlockLabelItem(path, r, index, m_problem, this);
  item->setPen(pen);
  addItem(item);
  item->refreshFixedSize(); // needs item->scene() (just set by addItem() above) to resolve the view's current scale
  item->setFlag(QGraphicsItem::ItemIsMovable);
  item->setFlag(QGraphicsItem::ItemIsSelectable);
  // Required for itemChange's ItemPositionChange/ItemPositionHasChanged
  // to fire at all (see NodeItem's identical flag) -- this was simply
  // missing here before, which is exactly why dragging a label never
  // synced its new position back into m_problem.
  item->setFlag(QGraphicsItem::ItemSendsGeometryChanges);
  // Matches NodeItem's z-value reasoning (see its comment) -- keeps a
  // label clickable/on-top even if it ends up visually over a segment or
  // arc, not exercised by today's bug but the same latent hazard.
  item->setZValue(1.0);
  item->setData(KindKey, static_cast<int>(FemmItemKind::BlockLabel));
  item->setData(IndexKey, index);
  m_settingInitialItemPosition = true;
  item->setPos(b.x, b.y);
  m_settingInitialItemPosition = false;
  m_blockLabelItems[index] = item;

  // Matches femm.rc's "Show Block Names" (ID_VIEW_SHOWNAMES) -- shows the
  // assigned material's name (or "<None>" for a hole) next to the label,
  // toggled via setShowBlockNames() rather than always drawn.
  QString labelText = (!isHole && b.blockTypeIndex >= 1 && b.blockTypeIndex <= m_problem->materialProps.size())
      ? m_problem->materialProps[b.blockTypeIndex - 1].name
      : QStringLiteral("<None>");
  auto* text = addSimpleText(labelText);
  text->setFlag(QGraphicsItem::ItemIgnoresTransformations);
  text->setBrush(isHole ? AppTheme::holeColor() : AppTheme::blockLabelNameColor());
  text->setPos(b.x, b.y);
  text->setVisible(m_showBlockNames);
  m_blockNameItems[index] = text;
}

void GeometryScene::addDimensionItem(int index)
{
  const FemmDimension& d = m_problem->dimensions[index];
  // Modified by Claude (Anthropic), noreply@anthropic.com: was
  // segmentColor() + Qt::DashLine ("distinct from ordinary geometry at a
  // glance, without a 5th color") -- per direct user request to match
  // Fusion 360's own dimension style (confirmed against a real Fusion 360
  // screenshot supplied as reference): committed dimensions there are
  // solid lines, not dashed. Solid now, with dimensionColor() taking over
  // the "distinct at a glance" job dashing used to do -- see that color's
  // own comment. The Smart Dimension ghost preview stays dashed/
  // selectedColor() on purpose (see updateSmartDimensionPreview) -- dashed
  // now meaningfully means "not committed yet" instead of just "this is a
  // dimension".
  QPen pen(AppTheme::dimensionColor());
  pen.setCosmetic(true);
  pen.setWidth(0);
  auto* item = new DimensionItem(index, m_problem); // sets KindKey/IndexKey itself, matching NodeItem's own constructor
  item->setPen(pen);
  addItem(item);

  auto* text = addSimpleText(QString());
  text->setFlag(QGraphicsItem::ItemIgnoresTransformations);
  text->setBrush(AppTheme::dimensionColor());
  // Modified by Claude (Anthropic), noreply@anthropic.com: was missing
  // entirely -- a REAL, pre-existing bug found while implementing Smart
  // Dimension's two-line-to-Angle upgrade (see handleToolClick's
  // SmartDimension case): an item with no KindKey/IndexKey data set
  // returns 0 for both from data(...).toInt() (QVariant's default int
  // conversion), and 0 is ALSO FemmItemKind::Node's own enum value --
  // so clicking a dimension's NUMBER LABEL (as opposed to its dashed
  // line, which the parent DimensionItem itself already tags correctly)
  // was silently misread as "clicked node index 0" everywhere: double-
  // click-to-edit, Select-mode clicks, and (concretely, this is what
  // surfaced it) Smart Dimension's angle-upgrade check, which need to
  // tell a genuine node click apart from an accidental hit on unrelated
  // label text sitting on top of it. Tagging the text the same as its
  // parent (Dimension, same index) makes clicking either one behave
  // identically, which is also the more intuitive behavior on its own
  // merits -- a dimension's number IS the dimension, visually.
  text->setData(KindKey, static_cast<int>(FemmItemKind::Dimension));
  text->setData(IndexKey, index);
  item->setTextItem(text);
  item->updateGeometry();
  m_dimensionItems[index] = item;

  QVector<int> touched;
  switch (d.type) {
  case DimensionType::Distance:
    touched = {d.refA, d.refB};
    break;
  case DimensionType::Radius:
    if (d.refA >= 0 && d.refA < m_problem->arcSegments.size()) {
      const FemmArcSegment& a = m_problem->arcSegments[d.refA];
      touched = {a.n0, a.n1};
    }
    break;
  case DimensionType::Angle:
    touched = {d.refA, d.refB, d.refC};
    break;
  case DimensionType::AngleLines:
    // Both segments' endpoints: moving any of the four changes the angle,
    // so all four must repaint this dimension.
    if (d.refA >= 0 && d.refA < m_problem->segments.size() && d.refB >= 0
        && d.refB < m_problem->segments.size()) {
      const FemmSegment& sa = m_problem->segments[d.refA];
      const FemmSegment& sb = m_problem->segments[d.refB];
      touched = {sa.n0, sa.n1, sb.n0, sb.n1};
    }
    break;
  }
  for (int n : touched)
    m_dimensionItemsByNode.insert(n, item);
}

void GeometryScene::addDistanceDimensionForNodes(int n0, int n1)
{
  if (n0 < 0 || n0 >= m_problem->nodes.size() || n1 < 0 || n1 >= m_problem->nodes.size() || n0 == n1)
    return;
  double dx = m_problem->nodes[n1].x - m_problem->nodes[n0].x;
  double dy = m_problem->nodes[n1].y - m_problem->nodes[n0].y;
  double curLen = std::hypot(dx, dy);
  bool ok = false;
  double value = QInputDialog::getDouble(views().isEmpty() ? nullptr : views().first(),
      "Distance Dimension", "Distance:", curLen, 0.0, 1.0e9, 6, &ok);
  if (!ok)
    return;
  emit aboutToEdit();
  FemmDimension dim;
  dim.type = DimensionType::Distance;
  dim.refA = n0;
  dim.refB = n1;
  dim.value = value;
  // Default dimension-line offset: perpendicular to the measured segment,
  // a modest fraction of its own length -- the user can reposition it
  // later (not implemented this round -- see the module's own scope
  // notes) by editing labelOffsetX/Y directly.
  if (curLen > 0) {
    dim.labelOffsetX = -dy / curLen * curLen * 0.15;
    dim.labelOffsetY = dx / curLen * curLen * 0.15;
  }
  m_problem->dimensions.push_back(dim);
  ConstraintSolver::SolveResult result = ConstraintSolver::solve(*m_problem);
  setConstraintStatus(result.nodeStatus);
  rebuild();
  emit problemEdited();
}

void GeometryScene::addRadiusDimensionForArc(int arcIndex)
{
  if (arcIndex < 0 || arcIndex >= m_problem->arcSegments.size())
    return;
  std::complex<double> c;
  double r = 0;
  if (!FemmProblemEdit::circleFromArc(*m_problem, m_problem->arcSegments[arcIndex], c, r))
    return;
  bool ok = false;
  double value = QInputDialog::getDouble(views().isEmpty() ? nullptr : views().first(),
      "Radius Dimension", "Radius:", r, 0.0001, 1.0e9, 6, &ok);
  if (!ok)
    return;
  emit aboutToEdit();
  FemmDimension dim;
  dim.type = DimensionType::Radius;
  dim.refA = arcIndex;
  dim.value = value;
  m_problem->dimensions.push_back(dim);
  ConstraintSolver::SolveResult result = ConstraintSolver::solve(*m_problem);
  setConstraintStatus(result.nodeStatus);
  rebuild();
  emit problemEdited();
}

// Modified by Claude (Anthropic), noreply@anthropic.com: recomputes the
// live Smart Dimension candidate for the current cursor position and
// refreshes its ghost preview item -- called from mouseMoveEvent() while
// m_smartDimAwaitingPlacement is true, and once more (with the click's own
// position) at the top of commitSmartDimensionPlacement() so the
// committed dimension always matches exactly what was last previewed.
//
// The Horizontal/Vertical/Aligned heuristic (only applied when
// m_smartDimTwoPointMode is true -- see that member's own comment; this
// now covers BOTH a 2-node-click candidate and a single-segment-length
// candidate, refA/refB being a plain point pair either way): the
// reference doc frames this as "the cursor resolves which geometric
// interpretation you intend" without spelling out the exact geometry, so
// this measures which of 3 candidate offset DIRECTIONS -- straight up/down
// (0,1) for Horizontal, straight left/right (1,0) for Vertical, or
// perpendicular to the P1-P2 line itself for Aligned -- the actual cursor
// offset from the two points' midpoint is most closely aligned with
// (largest |cos(angle)|, via a plain dot product since all vectors here
// are 2D). This matches the visual form each dimension actually takes: a
// Horizontal dimension's line sits directly above/below the two points
// (offset mostly vertical), a Vertical one to their side (offset mostly
// horizontal), and an Aligned one offset perpendicular to the segment
// they define -- for a single-segment candidate this Aligned case is
// exactly the segment's own true length, its witness/extension lines
// running perpendicular to it, same as any CAD tool's standard linear
// dimension.
void GeometryScene::updateSmartDimensionPreview(QPointF mousePos)
{
  if (!m_problem || !m_smartDimAwaitingPlacement)
    return;
  if (m_smartDimRefA < 0 || m_smartDimRefA >= m_problem->nodes.size())
    return;

  if (m_smartDimTwoPointMode) {
    if (m_smartDimRefB < 0 || m_smartDimRefB >= m_problem->nodes.size())
      return;
    const FemmNode& na = m_problem->nodes[m_smartDimRefA];
    const FemmNode& nb = m_problem->nodes[m_smartDimRefB];
    QPointF a(na.x, na.y), b(nb.x, nb.y);
    QPointF offsetVec = mousePos - (a + b) / 2.0;
    double offsetLen = std::hypot(offsetVec.x(), offsetVec.y());
    if (offsetLen > 1e-9) {
      QPointF lineDir = b - a;
      double lineLen = std::hypot(lineDir.x(), lineDir.y());
      QPointF alignedNormal = lineLen > 1e-9 ? QPointF(-lineDir.y(), lineDir.x()) / lineLen : QPointF(0, 1);
      double cosH = std::abs(offsetVec.y()) / offsetLen;
      double cosV = std::abs(offsetVec.x()) / offsetLen;
      double cosAligned = std::abs(offsetVec.x() * alignedNormal.x() + offsetVec.y() * alignedNormal.y()) / offsetLen;
      if (cosH >= cosV && cosH >= cosAligned)
        m_smartDimType = DimensionType::HorizontalDistance;
      else if (cosV >= cosH && cosV >= cosAligned)
        m_smartDimType = DimensionType::VerticalDistance;
      else
        m_smartDimType = DimensionType::Distance;
    }
  }

  // Build a TEMPORARY, not-yet-committed FemmDimension representing the
  // candidate as it stands right now, computing the same offset/direction
  // and live-measured value commitSmartDimensionPlacement() will use if
  // the user clicks this instant -- then hand it to buildDimensionPath(),
  // the exact same renderer a committed DimensionItem uses, so the
  // preview is pixel-identical to what actually gets created.
  FemmDimension preview;
  preview.type = m_smartDimType;
  preview.refA = m_smartDimRefA;
  preview.refB = m_smartDimRefB;
  preview.refC = m_smartDimRefC;

  switch (preview.type) {
  case DimensionType::Distance:
  case DimensionType::HorizontalDistance:
  case DimensionType::VerticalDistance: {
    if (preview.refB < 0 || preview.refB >= m_problem->nodes.size())
      return;
    const FemmNode& na = m_problem->nodes[preview.refA];
    const FemmNode& nb = m_problem->nodes[preview.refB];
    QPointF offset = mousePos - QPointF((na.x + nb.x) / 2.0, (na.y + nb.y) / 2.0);
    preview.labelOffsetX = offset.x();
    preview.labelOffsetY = offset.y();
    double dx = nb.x - na.x, dy = nb.y - na.y;
    preview.value = preview.type == DimensionType::HorizontalDistance ? std::abs(dx)
        : preview.type == DimensionType::VerticalDistance             ? std::abs(dy)
                                                                        : std::hypot(dx, dy);
    break;
  }
  case DimensionType::Radius: {
    if (preview.refA < 0 || preview.refA >= m_problem->arcSegments.size())
      return;
    std::complex<double> c;
    double r = 0;
    if (!FemmProblemEdit::circleFromArc(*m_problem, m_problem->arcSegments[preview.refA], c, r))
      return;
    QPointF center(c.real(), c.imag());
    QPointF dir = mousePos - center;
    double dirLen = std::hypot(dir.x(), dir.y());
    if (dirLen > 1e-9) {
      preview.labelOffsetX = dir.x() / dirLen;
      preview.labelOffsetY = dir.y() / dirLen;
    }
    preview.value = r;
    break;
  }
  case DimensionType::AngleLines: {
    QPointF v, r1, r2;
    if (!angleLinesFrame(*m_problem, preview, v, r1, r2))
      return;
    preview.value = angleLinesDegrees(v, r1, r2);
    break;
  }
  case DimensionType::Angle: {
    if (preview.refB < 0 || preview.refB >= m_problem->nodes.size() || preview.refC < 0 || preview.refC >= m_problem->nodes.size())
      return;
    const FemmNode& v = m_problem->nodes[preview.refA];
    const FemmNode& p1 = m_problem->nodes[preview.refB];
    const FemmNode& p2 = m_problem->nodes[preview.refC];
    double a1 = std::atan2(p1.y - v.y, p1.x - v.x);
    double a2 = std::atan2(p2.y - v.y, p2.x - v.x);
    double diff = a2 - a1;
    while (diff > M_PI)
      diff -= 2 * M_PI;
    while (diff <= -M_PI)
      diff += 2 * M_PI;
    preview.value = diff * 180.0 / M_PI;
    break;
  }
  }

  if (!m_smartDimPreviewItem) {
    auto* item = new QGraphicsPathItem();
    QPen pen(AppTheme::selectedColor());
    pen.setCosmetic(true);
    pen.setWidth(0);
    pen.setStyle(Qt::DashLine);
    item->setPen(pen);
    item->setOpacity(0.65); // "ghost" preview -- visually distinct from a committed dimension
    item->setZValue(3.0); // above everything, including constraint glyphs (2.0)
    // Modified by Claude (Anthropic), noreply@anthropic.com: makes this
    // item (and its text label below) completely invisible to itemAt()/
    // mouse hit-testing -- found while debugging why a second click meant
    // to hit a real segment (for the Length -> Angle upgrade) could
    // instead land on the PREVIEW ghost itself, since it's drawn on top
    // (z=3.0) of everything and, being freshly created here with no
    // KindKey/IndexKey ever set, would have been misread as "clicked node
    // index 0" (see addDimensionItem's own text-item comment for the
    // exact same QVariant-defaults-to-0 mechanism). Excluding it from hit-
    // testing entirely is more robust than tagging it correctly would
    // have been: a placement click should always see through the ghost
    // to whatever REAL geometry (or empty canvas) is actually underneath.
    item->setAcceptedMouseButtons(Qt::NoButton);
    addItem(item);
    m_smartDimPreviewItem = item;

    auto* text = addSimpleText(QString());
    text->setFlag(QGraphicsItem::ItemIgnoresTransformations);
    text->setBrush(AppTheme::selectedColor());
    text->setOpacity(0.85);
    text->setZValue(3.0);
    text->setAcceptedMouseButtons(Qt::NoButton);
    m_smartDimPreviewText = text;
  }
  QPointF textPos;
  QString text;
  QPainterPath path = buildDimensionPath(*m_problem, preview, textPos, text);
  static_cast<QGraphicsPathItem*>(m_smartDimPreviewItem)->setPath(path);
  static_cast<QGraphicsSimpleTextItem*>(m_smartDimPreviewText)->setText(text);
  static_cast<QGraphicsSimpleTextItem*>(m_smartDimPreviewText)->setPos(textPos);
}

void GeometryScene::commitSmartDimensionPlacement(QPointF mousePos)
{
  if (!m_problem || !m_smartDimAwaitingPlacement) {
    cancelSmartDimensionPreview();
    return;
  }
  // Make sure the candidate reflects the FINAL mouse position (this click)
  // before reading anything back out of it -- the last mouseMoveEvent may
  // have fired for a slightly earlier position.
  updateSmartDimensionPreview(mousePos);

  DimensionType type = m_smartDimType;
  int refA = m_smartDimRefA, refB = m_smartDimRefB, refC = m_smartDimRefC;
  QPointF offsetOrDir;
  QString label = "Distance:";
  double curValue = 0, minVal = 0, maxVal = 1.0e9;

  switch (type) {
  case DimensionType::Distance:
  case DimensionType::HorizontalDistance:
  case DimensionType::VerticalDistance: {
    if (refA < 0 || refA >= m_problem->nodes.size() || refB < 0 || refB >= m_problem->nodes.size()) {
      cancelSmartDimensionPreview();
      return;
    }
    const FemmNode& na = m_problem->nodes[refA];
    const FemmNode& nb = m_problem->nodes[refB];
    offsetOrDir = mousePos - QPointF((na.x + nb.x) / 2.0, (na.y + nb.y) / 2.0);
    double dx = nb.x - na.x, dy = nb.y - na.y;
    curValue = type == DimensionType::HorizontalDistance ? std::abs(dx)
        : type == DimensionType::VerticalDistance         ? std::abs(dy)
                                                            : std::hypot(dx, dy);
    break;
  }
  case DimensionType::Radius: {
    if (refA < 0 || refA >= m_problem->arcSegments.size()) {
      cancelSmartDimensionPreview();
      return;
    }
    std::complex<double> c;
    double r = 0;
    if (!FemmProblemEdit::circleFromArc(*m_problem, m_problem->arcSegments[refA], c, r)) {
      cancelSmartDimensionPreview();
      return;
    }
    QPointF center(c.real(), c.imag());
    QPointF dir = mousePos - center;
    double dirLen = std::hypot(dir.x(), dir.y());
    if (dirLen > 1e-9)
      offsetOrDir = dir / dirLen;
    curValue = r;
    label = "Radius:";
    break;
  }
  case DimensionType::AngleLines: {
    FemmDimension probe;
    probe.type = DimensionType::AngleLines;
    probe.refA = refA;
    probe.refB = refB;
    QPointF v, r1, r2;
    if (!angleLinesFrame(*m_problem, probe, v, r1, r2)) {
      cancelSmartDimensionPreview();
      return;
    }
    offsetOrDir = mousePos - v;
    curValue = angleLinesDegrees(v, r1, r2);
    label = "Angle (deg):";
    minVal = 0.01;
    maxVal = 179.99;
    break;
  }
  case DimensionType::Angle: {
    if (refA < 0 || refA >= m_problem->nodes.size() || refB < 0 || refB >= m_problem->nodes.size() || refC < 0 || refC >= m_problem->nodes.size()) {
      cancelSmartDimensionPreview();
      return;
    }
    const FemmNode& v = m_problem->nodes[refA];
    const FemmNode& p1 = m_problem->nodes[refB];
    const FemmNode& p2 = m_problem->nodes[refC];
    double a1 = std::atan2(p1.y - v.y, p1.x - v.x);
    double a2 = std::atan2(p2.y - v.y, p2.x - v.x);
    double diff = a2 - a1;
    while (diff > M_PI)
      diff -= 2 * M_PI;
    while (diff <= -M_PI)
      diff += 2 * M_PI;
    curValue = diff * 180.0 / M_PI;
    label = "Angle (deg):";
    minVal = -359.99;
    maxVal = 359.99;
    break;
  }
  }

  // Remove the ghost preview BEFORE showing a modal dialog, so it doesn't
  // linger on screen behind it -- also resets all the smart-dim state,
  // which is fine, since refA/refB/refC/type were already captured above.
  cancelSmartDimensionPreview();

  bool ok = false;
  double value = QInputDialog::getDouble(views().isEmpty() ? nullptr : views().first(),
      "Smart Dimension", label, curValue, minVal, maxVal, 6, &ok);
  if (!ok)
    return;

  emit aboutToEdit();
  FemmDimension dim;
  dim.type = type;
  dim.refA = refA;
  dim.refB = refB;
  dim.refC = refC;
  dim.value = value;
  dim.labelOffsetX = offsetOrDir.x();
  dim.labelOffsetY = offsetOrDir.y();
  m_problem->dimensions.push_back(dim);
  ConstraintSolver::SolveResult result = ConstraintSolver::solve(*m_problem);
  setConstraintStatus(result.nodeStatus);
  rebuild();
  emit problemEdited();
}

void GeometryScene::cancelSmartDimensionPreview()
{
  delete m_smartDimPreviewItem; // QGraphicsItem's destructor detaches itself from the scene automatically
  m_smartDimPreviewItem = nullptr;
  delete m_smartDimPreviewText;
  m_smartDimPreviewText = nullptr;
  m_smartDimAwaitingPlacement = false;
  m_smartDimTwoPointMode = false;
  m_smartDimAngleEligible = false;
  m_smartDimRefA = m_smartDimRefB = m_smartDimRefC = -1;
}

void GeometryScene::addConstraintItem(int index)
{
  const FemmConstraint& c = m_problem->constraints[index];
  auto* item = new ConstraintGlyphItem(index, m_problem); // sets KindKey/IndexKey itself, matching DimensionItem's own constructor
  addItem(item);
  item->refreshFixedSize(); // needs item->scene() (just set by addItem() above) to resolve the view's current scale

  double x = 0, y = 0;
  ConstraintSolver::anchorPoint(*m_problem, c, x, y);
  item->setPos(x, y); // plain QGraphicsItem, no itemChange override -- unlike NodeItem/BlockLabelItem, setPos() here has no side effects to guard against
  m_constraintItems[index] = item;

  const QVector<int> touched = ConstraintSolver::touchedNodes(*m_problem, c);
  for (int n : touched)
    m_constraintItemsByNode.insert(n, item);
}

void GeometryScene::updateSegmentItemGeometry(QGraphicsItem* item, int segmentIndex)
{
  const FemmSegment& s = m_problem->segments[segmentIndex];
  if (s.n0 < 0 || s.n0 >= m_problem->nodes.size() || s.n1 < 0 || s.n1 >= m_problem->nodes.size())
    return;
  const FemmNode& a = m_problem->nodes[s.n0];
  const FemmNode& b = m_problem->nodes[s.n1];
  static_cast<QGraphicsLineItem*>(item)->setLine(a.x, a.y, b.x, b.y);
}

void GeometryScene::updateArcItemGeometry(QGraphicsItem* item, int arcIndex)
{
  const FemmArcSegment& arc = m_problem->arcSegments[arcIndex];
  if (arc.n0 < 0 || arc.n0 >= m_problem->nodes.size() || arc.n1 < 0 || arc.n1 >= m_problem->nodes.size())
    return;
  const FemmNode& a = m_problem->nodes[arc.n0];
  const FemmNode& b = m_problem->nodes[arc.n1];

  double cx, cy, R, startAngleDeg;
  QPainterPath path;
  if (arcGeometry(a.x, a.y, b.x, b.y, arc.arcLength, cx, cy, R, startAngleDeg)) {
    path.moveTo(a.x, a.y);
    // Modified by Claude (Anthropic), noreply@anthropic.com: was
    // `arc.arcLength` (unnegated) -- confirmed live, with a debug print of
    // QPainterPath::currentPosition() after arcTo(), that the path did NOT
    // end at n1: a real 90-degree arc from a straight_wire_field.fem test
    // file (n0=(20,0), n1=(0,20)) rendered ending at (0,-20) instead.
    // startAngleDeg's own negation (see arcGeometry's comment) correctly
    // compensates the STARTING point for this view's scale(1,-1) y-flip
    // (GeometryView::GeometryView), but a plain-math-CCW sweep computed
    // from y-up node coordinates becomes visually CW once that same flip
    // is applied to the whole path -- so the sweep needs the identical
    // compensation the start angle already gets. This went unnoticed all
    // session because every arc actually exercised happened to be
    // direction-insensitive: exactly 180 degrees (immune, since +180 and
    // -180 land on the same point), or part of a heavily-overlapping,
    // rotationally-symmetric multi-arc assembly (this same wire file's
    // full circle) where one arc silently ending at the wrong point left
    // no visible gap because another arc's stroke already covered that
    // same screen position. Create Radius's fillet arc (a single,
    // asymmetric, non-multiple-of-180 arc with nothing else nearby to
    // mask it) is what actually exposed this.
    path.arcTo(cx - R, cy - R, 2 * R, 2 * R, startAngleDeg, -arc.arcLength);
  }
  static_cast<QGraphicsPathItem*>(item)->setPath(path);
}

void GeometryScene::onNodeMoved(int nodeIndex)
{
  const auto segItems = m_segmentItemsByNode.values(nodeIndex);
  for (QGraphicsItem* item : segItems)
    updateSegmentItemGeometry(item, item->data(IndexKey).toInt());

  const auto arcItems = m_arcItemsByNode.values(nodeIndex);
  for (QGraphicsItem* item : arcItems)
    updateArcItemGeometry(item, item->data(IndexKey).toInt());

  const auto dimItems = m_dimensionItemsByNode.values(nodeIndex);
  for (QGraphicsItem* item : dimItems)
    static_cast<DimensionItem*>(item)->updateGeometry();

  const auto constraintItems = m_constraintItemsByNode.values(nodeIndex);
  for (QGraphicsItem* item : constraintItems) {
    int constraintIndex = item->data(IndexKey).toInt();
    if (constraintIndex < 0 || constraintIndex >= m_problem->constraints.size())
      continue;
    double x = 0, y = 0;
    ConstraintSolver::anchorPoint(*m_problem, m_problem->constraints[constraintIndex], x, y);
    item->setPos(x, y);
  }

  emit problemEdited();
}

void GeometryScene::onBlockLabelMoved(int index)
{
  auto it = m_blockNameItems.find(index);
  if (it != m_blockNameItems.end() && it.value() && index >= 0 && index < m_problem->blockLabels.size()) {
    const FemmBlockLabel& b = m_problem->blockLabels[index];
    it.value()->setPos(b.x, b.y);
  }
  emit problemEdited();
}

void GeometryScene::snapshotOnceForDrag()
{
  if (m_dragSnapshotTaken)
    return;
  m_dragSnapshotTaken = true;
  emit aboutToEdit();
}

void GeometryScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
  if (event->button() == Qt::LeftButton && m_toolMode == GeometryToolMode::ZoomWindow) {
    m_zoomWindowStartPos = event->scenePos();
    if (!m_zoomWindowRectItem) {
      m_zoomWindowRectItem = new QGraphicsRectItem();
      QPen pen(Qt::darkGray, 0, Qt::DashLine);
      pen.setCosmetic(true);
      m_zoomWindowRectItem->setPen(pen);
      m_zoomWindowRectItem->setZValue(1000.0); // always on top while dragging
      addItem(m_zoomWindowRectItem);
    }
    m_zoomWindowRectItem->setRect(QRectF(m_zoomWindowStartPos, QSizeF(0, 0)));
    m_zoomWindowRectItem->setVisible(true);
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton && m_toolMode == GeometryToolMode::SelectCircle) {
    m_selectCircleStartPos = event->scenePos();
    if (!m_selectCircleItem) {
      m_selectCircleItem = new QGraphicsEllipseItem();
      QPen pen(Qt::darkGray, 0, Qt::DashLine);
      pen.setCosmetic(true);
      m_selectCircleItem->setPen(pen);
      m_selectCircleItem->setZValue(1000.0); // always on top while dragging
      addItem(m_selectCircleItem);
    }
    m_selectCircleItem->setRect(QRectF(m_selectCircleStartPos, QSizeF(0, 0)));
    m_selectCircleItem->setVisible(true);
    event->accept();
    return;
  }
  if (m_problem && event->button() == Qt::LeftButton && m_toolMode == GeometryToolMode::DrawRectangle) {
    m_drawRectStartPos = snapPoint(event->scenePos());
    if (!m_drawRectItem) {
      m_drawRectItem = new QGraphicsRectItem();
      QPen pen(Qt::darkGray, 0, Qt::DashLine);
      pen.setCosmetic(true);
      m_drawRectItem->setPen(pen);
      m_drawRectItem->setZValue(1000.0); // always on top while dragging
      addItem(m_drawRectItem);
    }
    m_drawRectItem->setRect(QRectF(m_drawRectStartPos, QSizeF(0, 0)));
    m_drawRectItem->setVisible(true);
    event->accept();
    return;
  }
  if (m_problem && event->button() == Qt::LeftButton && m_toolMode == GeometryToolMode::DrawCircle) {
    m_drawCircleStartPos = snapPoint(event->scenePos());
    if (!m_drawCircleItem) {
      m_drawCircleItem = new QGraphicsEllipseItem();
      QPen pen(Qt::darkGray, 0, Qt::DashLine);
      pen.setCosmetic(true);
      m_drawCircleItem->setPen(pen);
      m_drawCircleItem->setZValue(1000.0); // always on top while dragging
      addItem(m_drawCircleItem);
    }
    m_drawCircleItem->setRect(QRectF(m_drawCircleStartPos, QSizeF(0, 0)));
    m_drawCircleItem->setVisible(true);
    event->accept();
    return;
  }
  if (!m_problem || event->button() != Qt::LeftButton || m_toolMode == GeometryToolMode::Select) {
    QGraphicsScene::mousePressEvent(event);
    return;
  }
  handleToolClick(event);
}

void GeometryScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
  emit mousePositionChanged(snapPoint(event->scenePos()));

  if (m_toolMode == GeometryToolMode::SmartDimension && m_smartDimAwaitingPlacement) {
    // Deliberately the RAW (unsnapped) scene position -- dimension
    // placement/offset isn't grid-snapped even when geometry is, matching
    // every other dimension tool's own use of `pos` in handleToolClick.
    updateSmartDimensionPreview(event->scenePos());
    event->accept();
    return;
  }

  if (m_toolMode == GeometryToolMode::ZoomWindow && m_zoomWindowRectItem && m_zoomWindowRectItem->isVisible()) {
    m_zoomWindowRectItem->setRect(QRectF(m_zoomWindowStartPos, event->scenePos()).normalized());
    event->accept();
    return;
  }
  if (m_toolMode == GeometryToolMode::SelectCircle && m_selectCircleItem && m_selectCircleItem->isVisible()) {
    double r = QLineF(m_selectCircleStartPos, event->scenePos()).length();
    m_selectCircleItem->setRect(QRectF(m_selectCircleStartPos.x() - r, m_selectCircleStartPos.y() - r, 2 * r, 2 * r));
    event->accept();
    return;
  }
  if (m_toolMode == GeometryToolMode::DrawRectangle && m_drawRectItem && m_drawRectItem->isVisible()) {
    m_drawRectItem->setRect(QRectF(m_drawRectStartPos, snapPoint(event->scenePos())).normalized());
    event->accept();
    return;
  }
  if (m_toolMode == GeometryToolMode::DrawCircle && m_drawCircleItem && m_drawCircleItem->isVisible()) {
    double r = QLineF(m_drawCircleStartPos, snapPoint(event->scenePos())).length();
    m_drawCircleItem->setRect(QRectF(m_drawCircleStartPos.x() - r, m_drawCircleStartPos.y() - r, 2 * r, 2 * r));
    event->accept();
    return;
  }
  QGraphicsScene::mouseMoveEvent(event);
}

void GeometryScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
  // Modified by Claude (Anthropic), noreply@anthropic.com: re-solves once
  // per drag GESTURE (not per mouse-move frame), same "only if something
  // actually moved" gating snapshotOnceForDrag() already established --
  // m_dragSnapshotTaken is only ever set true by NodeItem/BlockLabelItem::
  // itemChange on a REAL position change, never by a plain click. A
  // conservative first cut per the plan this was built from (live
  // per-frame re-solving during the drag itself is a possible follow-up
  // once real-world solver speed is known); dragging a node that isn't
  // part of any constraint/dimension still re-solves (cheap -- the
  // solver's unknown set is scoped to constrained nodes only, so an
  // unrelated drag converges in 0 iterations) rather than tracking
  // exactly which node moved.
  bool dragEditOccurred = m_dragSnapshotTaken;
  // See snapshotOnceForDrag()'s comment -- whatever gesture this release
  // ends (a drag or just a plain click), the NEXT press starts a new one.
  m_dragSnapshotTaken = false;

  if (dragEditOccurred && m_problem && (!m_problem->constraints.isEmpty() || !m_problem->dimensions.isEmpty())) {
    ConstraintSolver::SolveResult result = ConstraintSolver::solve(*m_problem);
    setConstraintStatus(result.nodeStatus);
    rebuild();
  }

  if (m_toolMode == GeometryToolMode::ZoomWindow && m_zoomWindowRectItem && m_zoomWindowRectItem->isVisible()) {
    QRectF r = m_zoomWindowRectItem->rect();
    m_zoomWindowRectItem->setVisible(false);
    if (r.width() > 1e-9 && r.height() > 1e-9)
      emit zoomWindowSelected(r);
    setToolMode(GeometryToolMode::Select); // one-shot, mirrors FemmeView.cpp's ZoomWndFlag reset
    event->accept();
    return;
  }
  if (m_toolMode == GeometryToolMode::SelectCircle && m_selectCircleItem && m_selectCircleItem->isVisible()) {
    double r = QLineF(m_selectCircleStartPos, event->scenePos()).length();
    m_selectCircleItem->setVisible(false);
    if (r > 1e-9)
      selectByCircle(m_selectCircleStartPos, r);
    setToolMode(GeometryToolMode::Select); // one-shot, mirrors FemmeView.cpp's SelectCircFlag reset
    emit selectByCircleCompleted();
    event->accept();
    return;
  }
  if (m_toolMode == GeometryToolMode::DrawRectangle && m_drawRectItem && m_drawRectItem->isVisible()) {
    QRectF r = QRectF(m_drawRectStartPos, snapPoint(event->scenePos())).normalized();
    m_drawRectItem->setVisible(false);
    if (r.width() > 1e-9 && r.height() > 1e-9) {
      // Persistent tool (see the enum's own comment) -- stays active for
      // the next rectangle, unlike ZoomWindow/SelectCircle above.
      emit aboutToEdit();
      int n0 = FemmProblemEdit::addNode(*m_problem, r.left(), r.top());
      int n1 = FemmProblemEdit::addNode(*m_problem, r.right(), r.top());
      int n2 = FemmProblemEdit::addNode(*m_problem, r.right(), r.bottom());
      int n3 = FemmProblemEdit::addNode(*m_problem, r.left(), r.bottom());
      addNodeItem(n0);
      addNodeItem(n1);
      addNodeItem(n2);
      addNodeItem(n3);
      addSegmentItem(FemmProblemEdit::addSegment(*m_problem, n0, n1));
      addSegmentItem(FemmProblemEdit::addSegment(*m_problem, n1, n2));
      addSegmentItem(FemmProblemEdit::addSegment(*m_problem, n2, n3));
      addSegmentItem(FemmProblemEdit::addSegment(*m_problem, n3, n0));
      // Same as the Add Segment tool: a rectangle dropped over existing
      // geometry crosses it, and those crossings need nodes too.
      if (FemmProblemEdit::splitIntersectingSegments(*m_problem) > 0)
        rebuild();
      emit problemEdited();
    }
    event->accept();
    return;
  }
  if (m_toolMode == GeometryToolMode::DrawCircle && m_drawCircleItem && m_drawCircleItem->isVisible()) {
    QPointF center = m_drawCircleStartPos;
    double r = QLineF(center, snapPoint(event->scenePos())).length();
    m_drawCircleItem->setVisible(false);
    if (r > 1e-9) {
      // Persistent tool, same as DrawRectangle above.
      emit aboutToEdit();
      int n0 = FemmProblemEdit::addNode(*m_problem, center.x() + r, center.y());
      int n1 = FemmProblemEdit::addNode(*m_problem, center.x() - r, center.y());
      addNodeItem(n0);
      addNodeItem(n1);
      // Two 180-degree arcs, reusing whatever mesh density Add Arc last
      // used (or its own default) -- see the enum's own comment for why
      // this exact node/arc layout matches the codebase's established
      // "full circle" convention.
      addArcItem(FemmProblemEdit::addArcSegment(*m_problem, n0, n1, 180.0, m_lastArcMaxSegDeg));
      addArcItem(FemmProblemEdit::addArcSegment(*m_problem, n1, n0, 180.0, m_lastArcMaxSegDeg));
      emit problemEdited();
    }
    event->accept();
    return;
  }
  QGraphicsScene::mouseReleaseEvent(event);
}

void GeometryScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
  // Qt's default double-click handling would otherwise swallow the second
  // press of two same-position clicks (delivering mouseDoubleClickEvent
  // instead of a second mousePressEvent) -- which is exactly the
  // "click node A, click node B, click node B again to start a new
  // segment from it" pattern a normal fan/star layout needs. Route
  // double-clicks through the same tool-click handling so repeated
  // clicks on the same spot behave like repeated single clicks, matching
  // what a user (or, confirmed during this session's testing, an
  // automated UI test double-clicking the same node) would expect.
  if (!m_problem || event->button() != Qt::LeftButton || m_toolMode == GeometryToolMode::Select) {
    if (m_problem && event->button() == Qt::LeftButton && m_toolMode == GeometryToolMode::Select) {
      QTransform deviceTransform = views().isEmpty() ? QTransform() : views().first()->viewportTransform();
      QGraphicsItem* hit = itemAt(event->scenePos(), deviceTransform);
      if (hit)
        emit entityDoubleClicked(static_cast<FemmItemKind>(hit->data(KindKey).toInt()), hit->data(IndexKey).toInt());
    }
    QGraphicsScene::mouseDoubleClickEvent(event);
    return;
  }
  handleToolClick(event);
}

void GeometryScene::handleToolClick(QGraphicsSceneMouseEvent* event)
{
  // Only the "place new geometry here" tools snap -- AddSegment/AddArc
  // use this same `pos` purely for hit-testing an EXISTING node below, and
  // snapping that would shift it away from the node's actual (possibly
  // off-grid) position, breaking the click.
  QPointF pos = event->scenePos();

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-25: per
  // user report ("the undo button does not work for all the drawing
  // operations") -- none of the 4 add-geometry tools below ever emitted
  // aboutToEdit(), so Undo (wired to that signal, see its own comment)
  // silently had no record of anything drawn with them; only Delete and a
  // handful of MainWindow menu commands (Move/Copy/Scale/Mirror/Create
  // Radius/Create Open Boundary/Import DXF) were ever covered. Each of
  // these is a one-shot action per click (unlike a drag, which needs the
  // once-per-gesture handling in snapshotOnceForDrag()), so a plain
  // `emit aboutToEdit()` right before the mutation is exactly right here.
  switch (m_toolMode) {
  case GeometryToolMode::AddNode: {
    QPointF snapped = snapPoint(pos);
    emit aboutToEdit();
    int idx = FemmProblemEdit::addNode(*m_problem, snapped.x(), snapped.y());
    addNodeItem(idx);
    emit problemEdited();
    break;
  }
  case GeometryToolMode::AddBlockLabel: {
    QPointF snapped = snapPoint(pos);
    emit aboutToEdit();
    int idx = FemmProblemEdit::addBlockLabel(*m_problem, snapped.x(), snapped.y());
    addBlockLabelItem(idx);
    emit problemEdited();
    break;
  }
  case GeometryToolMode::AddSegment: {
    // Pass the attached view's actual transform (not identity) since
    // node items use ItemIgnoresTransformations -- Qt needs the real
    // device transform to correctly map their fixed-pixel-size shape
    // back into scene coordinates for hit-testing.
    QTransform deviceTransform = views().isEmpty() ? QTransform() : views().first()->viewportTransform();
    QGraphicsItem* hit = itemAt(pos, deviceTransform);
    if (hit && hit->data(KindKey).toInt() == static_cast<int>(FemmItemKind::Node)) {
      int clickedNode = hit->data(IndexKey).toInt();
      if (m_pendingNode < 0) {
        m_pendingNode = clickedNode;
      } else if (m_pendingNode != clickedNode) {
        emit aboutToEdit();
        int idx = FemmProblemEdit::addSegment(*m_problem, m_pendingNode, clickedNode);
        addSegmentItem(idx);
        // Split any crossing this new segment created, inserting a
        // node at each -- see FemmProblemEdit::splitIntersectingSegments.
        // A full rebuild(), not addSegmentItem(), because splitting
        // renumbers and adds segments the scene has no items for.
        if (FemmProblemEdit::splitIntersectingSegments(*m_problem) > 0)
          rebuild();
        m_pendingNode = -1;
        emit problemEdited();
      }
    }
    break;
  }
  case GeometryToolMode::AddArc: {
    QTransform deviceTransform = views().isEmpty() ? QTransform() : views().first()->viewportTransform();
    QGraphicsItem* hit = itemAt(pos, deviceTransform);
    if (hit && hit->data(KindKey).toInt() == static_cast<int>(FemmItemKind::Node)) {
      int clickedNode = hit->data(IndexKey).toInt();
      if (m_pendingNode < 0) {
        m_pendingNode = clickedNode;
      } else if (m_pendingNode != clickedNode) {
        // Prompt for the included angle and max mesh segment (degrees)
        // right after the second node click -- mirrors CArcDlg's role in
        // femm/FemmeView.cpp:2072-2092, including remembering the
        // last-used values as this session's new defaults. Boundary
        // assignment isn't prompted for here, matching Add Segment's own
        // "set it afterward via double-click" pattern.
        bool ok = false;
        double arcAngle = QInputDialog::getDouble(views().isEmpty() ? nullptr : views().first(),
            "Add Arc", "Arc Angle (deg, n0 -> n1 counterclockwise):", m_lastArcAngleDeg, 0.01, 359.99, 2, &ok);
        if (ok) {
          double maxSeg = QInputDialog::getDouble(views().isEmpty() ? nullptr : views().first(),
              "Add Arc", "Max Segment (deg per mesh element):", m_lastArcMaxSegDeg, 0.01, 90.0, 2, &ok);
          if (ok) {
            m_lastArcAngleDeg = arcAngle;
            m_lastArcMaxSegDeg = maxSeg;
            emit aboutToEdit();
            int idx = FemmProblemEdit::addArcSegment(*m_problem, m_pendingNode, clickedNode, arcAngle, maxSeg);
            addArcItem(idx);
            emit problemEdited();
          }
        }
        m_pendingNode = -1;
      }
    }
    break;
  }
  case GeometryToolMode::AddDimensionDistance: {
    QTransform deviceTransform = views().isEmpty() ? QTransform() : views().first()->viewportTransform();
    QGraphicsItem* hit = itemAt(pos, deviceTransform);
    if (hit && hit->data(KindKey).toInt() == static_cast<int>(FemmItemKind::Node)) {
      int clickedNode = hit->data(IndexKey).toInt();
      if (!m_pendingDimensionNodes.contains(clickedNode))
        m_pendingDimensionNodes.push_back(clickedNode);
      if (m_pendingDimensionNodes.size() == 2) {
        addDistanceDimensionForNodes(m_pendingDimensionNodes[0], m_pendingDimensionNodes[1]);
        m_pendingDimensionNodes.clear();
      }
    }
    break;
  }
  case GeometryToolMode::AddDimensionRadius: {
    QTransform deviceTransform = views().isEmpty() ? QTransform() : views().first()->viewportTransform();
    QGraphicsItem* hit = itemAt(pos, deviceTransform);
    if (hit && hit->data(KindKey).toInt() == static_cast<int>(FemmItemKind::Arc))
      addRadiusDimensionForArc(hit->data(IndexKey).toInt());
    break;
  }
  case GeometryToolMode::SmartDimension: {
    QTransform deviceTransform = views().isEmpty() ? QTransform() : views().first()->viewportTransform();
    // Modified by Claude (Anthropic), noreply@anthropic.com: was
    // itemAt(pos, deviceTransform) -- the preview ghost is drawn on top of
    // everything (z=3.0) specifically so it's never visually hidden,
    // which means it's frequently exactly what a placement click
    // geometrically lands on. A plain itemAt() returns the TOPMOST item
    // only, so when that's the ghost's own ITEM or its TEXT label,
    // itemAt() alone has no way to report what's actually underneath --
    // confirmed directly: an angle-upgrade click aimed at a second
    // segment came back hitting the ghost's text label instead (both
    // sharing screen space near the first candidate's measured value),
    // and simply treating that as "no hit" (an earlier, insufficient fix)
    // silently swallowed the click rather than seeing through to the
    // segment beneath it -- exactly backwards from how a real placement
    // click should be evaluated. items() (which itemAt() is itself
    // documented as a value(0) shorthand for) returns every item under
    // the cursor in top-to-bottom order, so skipping the ghost's own 2
    // items and taking the next one finds the real geometry (or
    // genuinely empty canvas) underneath, regardless of which of the two
    // ghost pieces happened to be topmost.
    QGraphicsItem* hit = nullptr;
    const QList<QGraphicsItem*> hitList = items(pos, Qt::IntersectsItemShape, Qt::DescendingOrder, deviceTransform);
    for (QGraphicsItem* candidate : hitList) {
      if (candidate == m_smartDimPreviewItem || candidate == m_smartDimPreviewText)
        continue;
      hit = candidate;
      break;
    }
    FemmItemKind kind = hit ? static_cast<FemmItemKind>(hit->data(KindKey).toInt()) : FemmItemKind::Node;

    if (!m_smartDimAwaitingPlacement) {
      // ---- Stage 1: entity selection -- what CAN be measured ----------
      if (!hit)
        break; // clicked empty space with nothing selected yet -- no-op
      if (kind == FemmItemKind::Segment) {
        // Modified by Claude (Anthropic), noreply@anthropic.com: per direct
        // user request ("the linear dimensions, I want them either
        // vertical, horizontal, or perpendicular to the line being
        // dimensioned") -- a single-segment click now gets the SAME live
        // Horizontal/Vertical/Aligned cursor resolution a 2-node click
        // already had (previously locked to Distance/true-length only).
        // "Aligned" here still IS the
        // segment's own true length (refA/refB are literally its two
        // endpoints), so this is a strict superset of the old behavior --
        // nothing that used to work stops working. See
        // m_smartDimTwoPointMode's own comment for why this is independent
        // of Angle-upgrade eligibility (still segment-only, via
        // m_smartDimAngleEligible below).
        const FemmSegment& s = m_problem->segments[hit->data(IndexKey).toInt()];
        m_smartDimType = DimensionType::Distance;
        m_smartDimRefA = s.n0;
        m_smartDimRefB = s.n1;
        m_smartDimTwoPointMode = true;
        m_smartDimAngleEligible = true;
        // Kept so a second, NON-touching line click can build an
        // AngleLines dimension, which references segments rather
        // than the endpoint nodes recorded above.
        m_smartDimSegA = hit->data(IndexKey).toInt();
        m_smartDimAwaitingPlacement = true;
        updateSmartDimensionPreview(pos);
      } else if (kind == FemmItemKind::Arc) {
        m_smartDimType = DimensionType::Radius;
        m_smartDimRefA = hit->data(IndexKey).toInt();
        m_smartDimTwoPointMode = false;
        m_smartDimAngleEligible = false;
        m_smartDimAwaitingPlacement = true;
        updateSmartDimensionPreview(pos);
      } else if (kind == FemmItemKind::Node) {
        int clickedNode = hit->data(IndexKey).toInt();
        if (!m_pendingDimensionNodes.contains(clickedNode))
          m_pendingDimensionNodes.push_back(clickedNode);
        if (m_pendingDimensionNodes.size() == 2) {
          // Type re-resolved live on every subsequent mouse move -- see
          // updateSmartDimensionPreview()'s own comment for the
          // Horizontal/Vertical/Aligned heuristic. Not Angle-eligible: two
          // freely-clicked nodes aren't guaranteed to share a real segment.
          m_smartDimType = DimensionType::Distance;
          m_smartDimRefA = m_pendingDimensionNodes[0];
          m_smartDimRefB = m_pendingDimensionNodes[1];
          m_smartDimTwoPointMode = true;
          m_smartDimAngleEligible = false;
          m_pendingDimensionNodes.clear();
          m_smartDimAwaitingPlacement = true;
          updateSmartDimensionPreview(pos);
        }
      }
      break;
    }

    // ---- Stage 2: awaiting placement -- extend to Angle, or commit ----
    bool upgradedToAngle = false;
    if (hit && kind == FemmItemKind::Segment && m_smartDimAngleEligible) {
      // A second LINE click while the first line's length/H/V/Aligned
      // preview is showing upgrades the candidate to an Angle dimension --
      // per the reference doc's Section 9 ("select the first line...
      // select the second line") -- but only if the two segments share a
      // common endpoint node: FemmDimension's Angle type is defined as
      // vertex + 2 ray endpoints (see FemmProblem.h's own comment), not a
      // general angle between two arbitrary, possibly-disjoint lines -- a
      // deliberate, documented scope cut (the vast majority of real
      // sketch angle dimensions ARE between two lines meeting at a shared
      // corner). Gated on m_smartDimAngleEligible alone (not on which of
      // Distance/HorizontalDistance/VerticalDistance the cursor currently
      // happens to be resolving to) -- a full click on a real, connected
      // second segment is an unambiguous "I want an angle" regardless of
      // what the live preview was showing a moment before.
      const FemmSegment& s2 = m_problem->segments[hit->data(IndexKey).toInt()];
      int vertex = -1, ray1 = -1, ray2 = -1;
      if (s2.n0 == m_smartDimRefA || s2.n0 == m_smartDimRefB) {
        vertex = s2.n0;
        ray1 = (m_smartDimRefA == vertex) ? m_smartDimRefB : m_smartDimRefA;
        ray2 = s2.n1;
      } else if (s2.n1 == m_smartDimRefA || s2.n1 == m_smartDimRefB) {
        vertex = s2.n1;
        ray1 = (m_smartDimRefA == vertex) ? m_smartDimRefB : m_smartDimRefA;
        ray2 = s2.n0;
      }
      // Modified by Claude (Anthropic), noreply@anthropic.com: per direct
      // user report that "the angle tool does not always work well",
      // asking for Fusion 360's behaviour. Two lines that do NOT share an
      // endpoint used to fall through and be swallowed as a placement
      // click -- so angling a corner worked and angling anything else
      // silently did nothing, which is exactly the "not always". Fusion
      // dimensions non-touching lines against their VIRTUAL intersection;
      // DimensionType::AngleLines does that (see FemmProblem.h), so the
      // shared-vertex case is now just the special case rather than the
      // only one.
      if (vertex < 0) {
        m_smartDimType = DimensionType::AngleLines;
        m_smartDimRefA = m_smartDimSegA;
        m_smartDimRefB = hit->data(IndexKey).toInt();
        m_smartDimRefC = -1;
        m_smartDimTwoPointMode = false;
        // Parallel lines have no intersection and no meaningful angle;
        // updateSmartDimensionPreview() leaves the candidate alone in that
        // case, so treat it as an ordinary placement click instead of
        // arming a dimension that can never resolve.
        QPointF v0, r0, r2v;
        FemmDimension probe;
        probe.type = DimensionType::AngleLines;
        probe.refA = m_smartDimRefA;
        probe.refB = m_smartDimRefB;
        if (angleLinesFrame(*m_problem, probe, v0, r0, r2v)) {
          updateSmartDimensionPreview(pos);
          upgradedToAngle = true;
        }
      } else {
        m_smartDimType = DimensionType::Angle;
        m_smartDimRefA = vertex;
        m_smartDimRefB = ray1;
        m_smartDimRefC = ray2;
        // Modified by Claude (Anthropic), noreply@anthropic.com: REAL bug
        // found via live testing after broadening m_smartDimTwoPointMode to
        // also cover segment-length candidates (see that member's own
        // comment) -- a segment-length candidate upgrading to Angle here
        // left m_smartDimTwoPointMode still true from before the upgrade,
        // so updateSmartDimensionPreview()'s live Horizontal/Vertical/
        // Aligned re-resolution block ran again right below, using refA/
        // refB (now vertex/ray1 -- happens to still be 2 valid node
        // indices) and silently clobbered the Angle type this line just
        // set back to a distance type, discarding refC/ray2 entirely.
        // Angle never re-resolves by cursor position the way a distance
        // candidate does (only its offset/direction changes, see
        // m_smartDimTwoPointMode's own comment), so this must be false
        // from the moment of upgrade onward.
        m_smartDimTwoPointMode = false;
        updateSmartDimensionPreview(pos);
        upgradedToAngle = true;
      }
    }
    if (!upgradedToAngle)
      commitSmartDimensionPlacement(pos);
    break;
  }
  case GeometryToolMode::AddDimensionAngle: {
    QTransform deviceTransform = views().isEmpty() ? QTransform() : views().first()->viewportTransform();
    QGraphicsItem* hit = itemAt(pos, deviceTransform);
    if (hit && hit->data(KindKey).toInt() == static_cast<int>(FemmItemKind::Node)) {
      int clickedNode = hit->data(IndexKey).toInt();
      if (!m_pendingDimensionNodes.contains(clickedNode))
        m_pendingDimensionNodes.push_back(clickedNode);
      if (m_pendingDimensionNodes.size() == 3) {
        int v = m_pendingDimensionNodes[0], p1 = m_pendingDimensionNodes[1], p2 = m_pendingDimensionNodes[2];
        double a1 = std::atan2(m_problem->nodes[p1].y - m_problem->nodes[v].y, m_problem->nodes[p1].x - m_problem->nodes[v].x);
        double a2 = std::atan2(m_problem->nodes[p2].y - m_problem->nodes[v].y, m_problem->nodes[p2].x - m_problem->nodes[v].x);
        double diff = a2 - a1;
        while (diff > M_PI)
          diff -= 2 * M_PI;
        while (diff <= -M_PI)
          diff += 2 * M_PI;
        double curDeg = diff * 180.0 / M_PI;
        bool ok = false;
        double value = QInputDialog::getDouble(views().isEmpty() ? nullptr : views().first(),
            "Angle Dimension", "Angle (deg, ray1 -> ray2):", curDeg, -359.99, 359.99, 2, &ok);
        if (ok) {
          emit aboutToEdit();
          FemmDimension dim;
          dim.type = DimensionType::Angle;
          dim.refA = v;
          dim.refB = p1;
          dim.refC = p2;
          dim.value = value;
          m_problem->dimensions.push_back(dim);
          ConstraintSolver::SolveResult result = ConstraintSolver::solve(*m_problem);
          setConstraintStatus(result.nodeStatus);
          rebuild();
          emit problemEdited();
        }
        m_pendingDimensionNodes.clear();
      }
    }
    break;
  }
  default:
    break;
  }
}

void GeometryScene::keyPressEvent(QKeyEvent* event)
{
  if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
    deleteSelectedItem();
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Space) {
    emit openSelectedRequested();
    event->accept();
    return;
  }
  // Modified by Claude (Anthropic), noreply@anthropic.com: backs out of an
  // in-progress Smart Dimension placement without committing it -- the
  // reference doc's own workflow implicitly assumes you can back out
  // before the placement click (e.g. "Do not click yet" while inspecting
  // the preview, Section 26).
  if (event->key() == Qt::Key_Escape && m_smartDimAwaitingPlacement) {
    cancelSmartDimensionPreview();
    event->accept();
    return;
  }
  QGraphicsScene::keyPressEvent(event);
}

void GeometryScene::deleteSelectedItem()
{
  if (!m_problem)
    return;
  const auto selected = selectedItems();
  if (selected.isEmpty())
    return;

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // was one item per keypress (see the removed comment below for why:
  // deleting a node cascades to remove touching segments/arcs and
  // renumbers every remaining reference above it, which would invalidate
  // the rest of a naively-batched multi-selection's captured indices).
  // Per user request -- delete the WHOLE selection in one press. Safe
  // batching: bucket by kind first, then delete kind-by-kind in an order
  // where earlier deletions can never invalidate a later bucket's already-
  // -captured indices -- Segments/Arcs/BlockLabels first (each only
  // renumbers *within its own kind*, per FemmProblemEdit's
  // deleteSegment/deleteArcSegment/deleteBlockLabel, so deleting one kind
  // never shifts another's indices), Nodes last (FemmProblemEdit::
  // deleteNode is the only one that reaches into segments/arcs, but by
  // the time it runs, any segment/arc the user *also* selected is already
  // gone -- its cascade then only ever touches segments/arcs that were
  // NOT explicitly selected, which is exactly the "still attached,
  // implicitly removed" behavior a single-node delete already has).
  // WITHIN each kind, descending index order keeps every not-yet-deleted
  // index in that same kind valid, since removing a higher index never
  // shifts a lower one.
  QVector<int> nodeIdx, segIdx, arcIdx, blockIdx, dimIdx, constraintIdx;
  for (QGraphicsItem* item : selected) {
    auto kind = static_cast<FemmItemKind>(item->data(KindKey).toInt());
    int index = item->data(IndexKey).toInt();
    switch (kind) {
    case FemmItemKind::Node: nodeIdx.push_back(index); break;
    case FemmItemKind::Segment: segIdx.push_back(index); break;
    case FemmItemKind::Arc: arcIdx.push_back(index); break;
    case FemmItemKind::BlockLabel: blockIdx.push_back(index); break;
    case FemmItemKind::Dimension: dimIdx.push_back(index); break;
    case FemmItemKind::Constraint: constraintIdx.push_back(index); break;
    }
  }
  std::sort(nodeIdx.begin(), nodeIdx.end(), std::greater<int>());
  std::sort(segIdx.begin(), segIdx.end(), std::greater<int>());
  std::sort(arcIdx.begin(), arcIdx.end(), std::greater<int>());
  std::sort(blockIdx.begin(), blockIdx.end(), std::greater<int>());
  std::sort(dimIdx.begin(), dimIdx.end(), std::greater<int>());
  std::sort(constraintIdx.begin(), constraintIdx.end(), std::greater<int>());

  emit aboutToEdit();
  for (int i : dimIdx)
    FemmProblemEdit::deleteDimension(*m_problem, i);
  // Like dimensions, nothing else ever references a constraint BY INDEX --
  // safe to delete in any order relative to the other buckets.
  for (int i : constraintIdx)
    FemmProblemEdit::deleteConstraint(*m_problem, i);
  for (int i : blockIdx)
    FemmProblemEdit::deleteBlockLabel(*m_problem, i);
  for (int i : segIdx)
    FemmProblemEdit::deleteSegment(*m_problem, i);
  for (int i : arcIdx)
    FemmProblemEdit::deleteArcSegment(*m_problem, i);
  for (int i : nodeIdx)
    FemmProblemEdit::deleteNode(*m_problem, i);

  // A deleted constraint/dimension changes what the solver enforces even
  // though nothing was moved -- re-solve (cheap no-op if none remain) so
  // DOF-status coloring reflects the new, smaller constraint system
  // rather than showing stale classifications for geometry that's no
  // longer actually constrained the same way.
  if (!m_problem->constraints.isEmpty() || !m_problem->dimensions.isEmpty()) {
    ConstraintSolver::SolveResult result = ConstraintSolver::solve(*m_problem);
    setConstraintStatus(result.nodeStatus);
  } else {
    setConstraintStatus({});
  }

  rebuild();
  emit problemEdited();
}

void GeometryScene::syncSelectionToProblem()
{
  if (!m_problem)
    return;
  for (FemmNode& n : m_problem->nodes)
    n.isSelected = false;
  for (FemmSegment& s : m_problem->segments)
    s.isSelected = false;
  for (FemmArcSegment& a : m_problem->arcSegments)
    a.isSelected = false;
  for (FemmBlockLabel& b : m_problem->blockLabels)
    b.isSelected = false;

  const auto sel = selectedItems();
  for (QGraphicsItem* item : sel) {
    auto kind = static_cast<FemmItemKind>(item->data(KindKey).toInt());
    int index = item->data(IndexKey).toInt();
    switch (kind) {
    case FemmItemKind::Node:
      if (index >= 0 && index < m_problem->nodes.size())
        m_problem->nodes[index].isSelected = true;
      break;
    case FemmItemKind::Segment:
      if (index >= 0 && index < m_problem->segments.size())
        m_problem->segments[index].isSelected = true;
      break;
    case FemmItemKind::Arc:
      if (index >= 0 && index < m_problem->arcSegments.size())
        m_problem->arcSegments[index].isSelected = true;
      break;
    case FemmItemKind::BlockLabel:
      if (index >= 0 && index < m_problem->blockLabels.size())
        m_problem->blockLabels[index].isSelected = true;
      break;
    }
  }
}

bool GeometryScene::selectedEntities(FemmItemKind& kind, QVector<int>& indices) const
{
  const auto sel = selectedItems();
  if (sel.isEmpty())
    return false;
  kind = static_cast<FemmItemKind>(sel.first()->data(KindKey).toInt());
  QVector<int> out;
  out.reserve(sel.size());
  for (QGraphicsItem* item : sel) {
    if (static_cast<FemmItemKind>(item->data(KindKey).toInt()) != kind)
      return false; // mixed kinds -- not something classic FEMM's own selection model can produce
    out.push_back(item->data(IndexKey).toInt());
  }
  indices = out;
  return true;
}

void GeometryScene::selectedByKind(QVector<int>& nodes, QVector<int>& segments, QVector<int>& arcs, QVector<int>& blockLabels) const
{
  nodes.clear();
  segments.clear();
  arcs.clear();
  blockLabels.clear();
  const auto sel = selectedItems();
  for (QGraphicsItem* item : sel) {
    auto kind = static_cast<FemmItemKind>(item->data(KindKey).toInt());
    int index = item->data(IndexKey).toInt();
    switch (kind) {
    case FemmItemKind::Node: nodes.push_back(index); break;
    case FemmItemKind::Segment: segments.push_back(index); break;
    case FemmItemKind::Arc: arcs.push_back(index); break;
    case FemmItemKind::BlockLabel: blockLabels.push_back(index); break;
    case FemmItemKind::Dimension: // not a valid constraint target
    case FemmItemKind::Constraint: // ditto -- a glyph can't itself be an input to another constraint
      break;
    }
  }
}

void GeometryScene::setShowGrid(bool show)
{
  m_showGrid = show;
  resetViewBackgroundCache();
}

void GeometryScene::setGridSize(double size)
{
  if (size > 0)
    m_gridSize = size;
  resetViewBackgroundCache();
}

void GeometryScene::resetViewBackgroundCache()
{
  // GeometryView uses QGraphicsView::CacheBackground (see its constructor's
  // comment) -- that cache only regenerates on a view transform/resize, not
  // on a plain update() or even invalidate(QRectF(), BackgroundLayer)
  // (confirmed live: a null/default QRectF did NOT expand to "whole scene"
  // here the way QGraphicsScene::update()'s docs describe -- drawBackground
  // was re-entered but with a near-zero-area rect, so the cached pixmap's
  // stale "no grid" content was left untouched outside that sliver).
  // QGraphicsView::resetCachedContent() is the API actually documented for
  // this exact case ("content changes but items don't") and reliably forces
  // a full redraw on the next paint.
  for (QGraphicsView* view : views()) {
    view->resetCachedContent();
    view->viewport()->update();
  }
}

QPointF GeometryScene::snapPoint(QPointF p) const
{
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-12:
  // object snapping (issue #28). This was grid-only, which meant a
  // segment could not be started exactly on an existing node unless that
  // node happened to sit on the grid -- so users placed it approximately
  // and repaired it with a Coincident constraint, and geometry that only
  // LOOKS joined does not mesh as a bounded region.
  //
  // Grid snap is unchanged and still applies wherever no geometry is in
  // range: SnapEngine tries it last rather than instead (see its
  // priority comment).
  m_lastSnap = SnapEngine::SnapResult();

  if (!m_problem)
    return snapToGridOnly(p);

  unsigned flags = m_snapFlags;
  if (m_snapSuspended)
    flags = SnapEngine::SnapNone;
  if (!m_snapToGrid)
    flags &= ~(unsigned)SnapEngine::SnapGrid;
  if (flags == SnapEngine::SnapNone)
    return p;

  // The capture radius is specified in PIXELS and converted here, so the
  // distance that feels right on screen stays the same at every zoom.
  // The conversion lives at this call site rather than inside
  // SnapEngine, which has no business knowing about views.
  double radius = 0.0;
  if (!views().isEmpty()) {
    const qreal scale = views().first()->transform().m11();
    if (scale > 0)
      radius = kSnapCapturePixels / scale;
  }

  const SnapEngine::SnapResult r = SnapEngine::findSnap(*m_problem,
      p.x(), p.y(), radius, flags, m_gridSize,
      m_snapReferenceValid, m_snapReference.x(), m_snapReference.y());
  m_lastSnap = r;
  if (!r.snapped())
    return p;
  return QPointF(r.x, r.y);
}

QPointF GeometryScene::snapToGridOnly(QPointF p) const
{
  if (!m_snapToGrid || m_gridSize <= 0)
    return p;
  return QPointF(std::round(p.x() / m_gridSize) * m_gridSize,
      std::round(p.y() / m_gridSize) * m_gridSize);
}

void GeometryScene::setShowBlockNames(bool show)
{
  m_showBlockNames = show;
  for (auto it = m_blockNameItems.constBegin(); it != m_blockNameItems.constEnd(); ++it)
    it.value()->setVisible(show);
}

void GeometryScene::setMeshOverlay(const MeshOverlay& mesh)
{
  clearMeshOverlay();
  if (mesh.elements.isEmpty())
    return;

  // m_mesh owns the data for the item's lifetime -- see MeshOverlayItem's
  // header comment for why this replaced a single monolithic
  // QGraphicsPathItem built from the whole mesh regardless of zoom.
  m_mesh = mesh;
  auto* item = new MeshOverlayItem(&m_mesh);
  item->setZValue(-1.0); // beneath geometry (nodes/segments/arcs/labels)
  item->setVisible(m_showMesh);
  addItem(item);
  m_meshOverlayItem = item;
}

void GeometryScene::clearMeshOverlay()
{
  if (m_meshOverlayItem) {
    removeItem(m_meshOverlayItem);
    delete m_meshOverlayItem;
    m_meshOverlayItem = nullptr;
  }
  m_mesh = MeshOverlay();
}

void GeometryScene::setShowMesh(bool show)
{
  m_showMesh = show;
  if (m_meshOverlayItem)
    m_meshOverlayItem->setVisible(show);
}

bool GeometryScene::selectOrphans()
{
  if (!m_problem)
    return false;

  clearSelection();

  QHash<int, int> touchCount;
  for (const FemmSegment& s : m_problem->segments) {
    touchCount[s.n0]++;
    touchCount[s.n1]++;
  }
  for (const FemmArcSegment& a : m_problem->arcSegments) {
    touchCount[a.n0]++;
    touchCount[a.n1]++;
  }

  bool foundAny = false;
  for (auto it = touchCount.constBegin(); it != touchCount.constEnd(); ++it) {
    if (it.value() != 1)
      continue;
    foundAny = true;
    int nodeIdx = it.key();
    if (m_nodeItems.contains(nodeIdx))
      m_nodeItems[nodeIdx]->setSelected(true);
    const auto segItems = m_segmentItemsByNode.values(nodeIdx);
    for (QGraphicsItem* item : segItems)
      item->setSelected(true);
    const auto arcItems = m_arcItemsByNode.values(nodeIdx);
    for (QGraphicsItem* item : arcItems)
      item->setSelected(true);
  }
  return foundAny;
}

void GeometryScene::selectByGroup(int groupNumber)
{
  if (!m_problem)
    return;
  clearSelection();
  const auto all = items();
  for (QGraphicsItem* item : all) {
    auto kind = static_cast<FemmItemKind>(item->data(KindKey).toInt());
    int index = item->data(IndexKey).toInt();
    bool matches = false;
    switch (kind) {
    case FemmItemKind::Node: matches = index >= 0 && index < m_problem->nodes.size() && m_problem->nodes[index].inGroup == groupNumber; break;
    case FemmItemKind::Segment: matches = index >= 0 && index < m_problem->segments.size() && m_problem->segments[index].inGroup == groupNumber; break;
    case FemmItemKind::Arc: matches = index >= 0 && index < m_problem->arcSegments.size() && m_problem->arcSegments[index].inGroup == groupNumber; break;
    case FemmItemKind::BlockLabel: matches = index >= 0 && index < m_problem->blockLabels.size() && m_problem->blockLabels[index].inGroup == groupNumber; break;
    }
    if (matches)
      item->setSelected(true);
  }
}

bool GeometryScene::selectByCircle(QPointF center, double radius)
{
  if (!m_problem)
    return false;
  clearSelection();
  auto within = [&](double x, double y) { return QLineF(center, QPointF(x, y)).length() <= radius; };
  bool foundAny = false;
  const auto all = items();
  for (QGraphicsItem* item : all) {
    auto kind = static_cast<FemmItemKind>(item->data(KindKey).toInt());
    int index = item->data(IndexKey).toInt();
    bool matches = false;
    switch (kind) {
    case FemmItemKind::Node:
      matches = index >= 0 && index < m_problem->nodes.size() && within(m_problem->nodes[index].x, m_problem->nodes[index].y);
      break;
    case FemmItemKind::BlockLabel:
      matches = index >= 0 && index < m_problem->blockLabels.size() && within(m_problem->blockLabels[index].x, m_problem->blockLabels[index].y);
      break;
    case FemmItemKind::Segment:
      if (index >= 0 && index < m_problem->segments.size()) {
        const FemmSegment& s = m_problem->segments[index];
        if (s.n0 >= 0 && s.n0 < m_problem->nodes.size() && s.n1 >= 0 && s.n1 < m_problem->nodes.size())
          matches = within(m_problem->nodes[s.n0].x, m_problem->nodes[s.n0].y) && within(m_problem->nodes[s.n1].x, m_problem->nodes[s.n1].y);
      }
      break;
    case FemmItemKind::Arc:
      if (index >= 0 && index < m_problem->arcSegments.size()) {
        const FemmArcSegment& a = m_problem->arcSegments[index];
        if (a.n0 >= 0 && a.n0 < m_problem->nodes.size() && a.n1 >= 0 && a.n1 < m_problem->nodes.size())
          matches = within(m_problem->nodes[a.n0].x, m_problem->nodes[a.n0].y) && within(m_problem->nodes[a.n1].x, m_problem->nodes[a.n1].y);
      }
      break;
    }
    if (matches) {
      item->setSelected(true);
      foundAny = true;
    }
  }
  return foundAny;
}

void GeometryScene::applyGroupToSelected(int groupNumber)
{
  if (!m_problem)
    return;
  const auto sel = selectedItems();
  for (QGraphicsItem* item : sel) {
    auto kind = static_cast<FemmItemKind>(item->data(KindKey).toInt());
    int index = item->data(IndexKey).toInt();
    switch (kind) {
    case FemmItemKind::Node:
      if (index >= 0 && index < m_problem->nodes.size())
        m_problem->nodes[index].inGroup = groupNumber;
      break;
    case FemmItemKind::Segment:
      if (index >= 0 && index < m_problem->segments.size())
        m_problem->segments[index].inGroup = groupNumber;
      break;
    case FemmItemKind::Arc:
      if (index >= 0 && index < m_problem->arcSegments.size())
        m_problem->arcSegments[index].inGroup = groupNumber;
      break;
    case FemmItemKind::BlockLabel:
      if (index >= 0 && index < m_problem->blockLabels.size())
        m_problem->blockLabels[index].inGroup = groupNumber;
      break;
    }
  }
}

void GeometryScene::drawBackground(QPainter* painter, const QRectF& rect)
{
  QGraphicsScene::drawBackground(painter, rect);
  if (!m_showGrid || m_gridSize <= 0)
    return;

  // Modified by Claude (Anthropic), noreply@anthropic.com: per direct user
  // request, following up on Set Grid's Cartesian/Polar combo (see
  // MainWindow::onSetGridTriggered) actually changing what the grid looks
  // like when Polar is selected, not just Enter Point's field labels and
  // the status bar readout. Confirmed directly against femm/FemmeView.cpp
  // (OnDraw's grid-drawing block, and the plain x/y snap in OnLButtonUp)
  // that classic FEMM has no visual polar grid at all -- Coords/polar
  // there never affects rendering or snapping, only those two read-outs
  // -- so this is a new, deliberate addition (concentric rings spaced by
  // the same Grid Size, spokes every 15 degrees), not a classic port.
  if (m_problem && m_problem->coordsPolar) {
    double maxR = std::hypot(std::max(std::abs(rect.left()), std::abs(rect.right())),
        std::max(std::abs(rect.top()), std::abs(rect.bottom())));
    int nRings = static_cast<int>(std::ceil(maxR / m_gridSize));
    if (nRings > 2000) // matches the cartesian grid's own too-dense-to-draw bailout
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

  double x0 = std::floor(rect.left() / m_gridSize) * m_gridSize;
  double y0 = std::floor(rect.top() / m_gridSize) * m_gridSize;
  int nx = static_cast<int>(std::ceil(rect.width() / m_gridSize)) + 2;
  int ny = static_cast<int>(std::ceil(rect.height() / m_gridSize)) + 2;
  // Too zoomed out for the grid to be a useful visual (an unreadable solid
  // mass of dots) or to draw quickly -- matches common CAD-editor behavior
  // of auto-hiding a fine grid at low zoom rather than a hard error.
  if ((qint64)nx * (qint64)ny > 200000)
    return;

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-22:
  // was QPainter::drawPoint() with a wide cosmetic pen -- confirmed
  // directly (after also trying a QPen width/color fix and a RoundCap
  // fix, neither of which helped) that Show Grid produced zero visible
  // dots at any zoom level or grid spacing on this build, despite the
  // toggle itself being verified correct. This view's viewport is a
  // QOpenGLWidget (GeometryView.cpp, FEMMQT_HAVE_OPENGL) -- Qt's OpenGL
  // paint engine implements drawPoint() via its own GL point-rendering
  // path, which does not reliably honor QPen width/cap-style the way the
  // default raster engine does. Switched to the same fixed-screen-pixel-
  // radius filled drawEllipse() pattern already used and proven
  // elsewhere in this exact codebase (e.g. MeshSolutionItem's "Show
  // Points", SolutionView.cpp) instead of a second, differently-behaved
  // primitive.
  double screenScale = painter->worldTransform().m11();
  double r = 1.0 / std::max(screenScale, 1e-9); // 2px-diameter dot, matching the old pen width
  painter->setPen(Qt::NoPen);
  painter->setBrush(AppTheme::gridLine());
  for (int i = 0; i < nx; i++) {
    double x = x0 + i * m_gridSize;
    for (int j = 0; j < ny; j++)
      painter->drawEllipse(QPointF(x, y0 + j * m_gridSize), r, r);
  }
}
