#define _USE_MATH_DEFINES

#include "GeometryScene.h"

#include "AppTheme.h"
#include "FemmProblem.h"
#include "FemmProblemEdit.h"
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

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

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

} // namespace

GeometryScene::GeometryScene(QObject* parent)
    : QGraphicsScene(parent)
{
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
  m_zoomWindowRectItem = nullptr;
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
}

void GeometryScene::setToolMode(GeometryToolMode mode)
{
  m_toolMode = mode;
  m_pendingNode = -1;
}

void GeometryScene::addNodeItem(int index)
{
  const FemmNode& n = m_problem->nodes[index];
  QPen pen(AppTheme::nodeColor());
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
  item->setBrush(QBrush(AppTheme::nodeColor()));
  addItem(item);
  item->refreshFixedSize(); // needs item->scene() (just set by addItem() above) to resolve the view's current scale
  m_nodeItems[index] = item;
}

void GeometryScene::addSegmentItem(int index)
{
  const FemmSegment& s = m_problem->segments[index];
  QPen pen(s.boundaryMarker != 0 ? AppTheme::boundaryEdgeColor() : AppTheme::segmentColor());
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
  QPen pen(a.boundaryMarker != 0 ? AppTheme::boundaryEdgeColor() : AppTheme::arcColor());
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
  if (!m_problem || event->button() != Qt::LeftButton || m_toolMode == GeometryToolMode::Select) {
    QGraphicsScene::mousePressEvent(event);
    return;
  }
  handleToolClick(event);
}

void GeometryScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
  emit mousePositionChanged(snapPoint(event->scenePos()));

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
  QGraphicsScene::mouseMoveEvent(event);
}

void GeometryScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
  // See snapshotOnceForDrag()'s comment -- whatever gesture this release
  // ends (a drag or just a plain click), the NEXT press starts a new one.
  m_dragSnapshotTaken = false;

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
  QVector<int> nodeIdx, segIdx, arcIdx, blockIdx;
  for (QGraphicsItem* item : selected) {
    auto kind = static_cast<FemmItemKind>(item->data(KindKey).toInt());
    int index = item->data(IndexKey).toInt();
    switch (kind) {
    case FemmItemKind::Node: nodeIdx.push_back(index); break;
    case FemmItemKind::Segment: segIdx.push_back(index); break;
    case FemmItemKind::Arc: arcIdx.push_back(index); break;
    case FemmItemKind::BlockLabel: blockIdx.push_back(index); break;
    }
  }
  std::sort(nodeIdx.begin(), nodeIdx.end(), std::greater<int>());
  std::sort(segIdx.begin(), segIdx.end(), std::greater<int>());
  std::sort(arcIdx.begin(), arcIdx.end(), std::greater<int>());
  std::sort(blockIdx.begin(), blockIdx.end(), std::greater<int>());

  emit aboutToEdit();
  for (int i : blockIdx)
    FemmProblemEdit::deleteBlockLabel(*m_problem, i);
  for (int i : segIdx)
    FemmProblemEdit::deleteSegment(*m_problem, i);
  for (int i : arcIdx)
    FemmProblemEdit::deleteArcSegment(*m_problem, i);
  for (int i : nodeIdx)
    FemmProblemEdit::deleteNode(*m_problem, i);

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

void GeometryScene::setShowGrid(bool show)
{
  m_showGrid = show;
  update();
}

void GeometryScene::setGridSize(double size)
{
  if (size > 0)
    m_gridSize = size;
  update();
}

QPointF GeometryScene::snapPoint(QPointF p) const
{
  if (!m_snapToGrid || m_gridSize <= 0)
    return p;
  return QPointF(std::round(p.x() / m_gridSize) * m_gridSize, std::round(p.y() / m_gridSize) * m_gridSize);
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
