#define _USE_MATH_DEFINES

#include "GeometryScene.h"

#include "FemmProblem.h"
#include "FemmProblemEdit.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QKeyEvent>
#include <QPainterPath>
#include <QPen>

#include <cmath>

namespace {

constexpr int KindKey = 0;
constexpr int IndexKey = 1;

// Fixed screen-pixel sizes for node/block-label handles -- see the
// ItemIgnoresTransformations comment in NodeItem below for why these are
// pixel constants rather than a world-space fraction of the model's
// bounding box.
constexpr double kNodeHandlePixelRadius = 5.0;
constexpr double kBlockLabelPixelRadius = 6.0;

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
    // Keep node handles a constant screen size regardless of view zoom --
    // without this, a node's world-space radius (a small fraction of the
    // model's bounding box, see g_nodeRadius) can render as 1-2 screen
    // pixels for typical zoom levels, making nodes nearly impossible to
    // click precisely (confirmed directly: automated clicks landing
    // exactly on a node's last-known position still missed it). Standard
    // CAD-editor behavior is fixed-size selection handles independent of
    // zoom, not world-scaled ones -- safe for a symmetric circle since
    // the view's y-flip (see MainWindow's view->scale(1,-1)) doesn't
    // change how a circle looks either way.
    setFlag(QGraphicsItem::ItemIgnoresTransformations);
    setData(KindKey, static_cast<int>(FemmItemKind::Node));
    setData(IndexKey, nodeIndex);
  }

  protected:
  QVariant itemChange(GraphicsItemChange change, const QVariant& value) override
  {
    if (change == ItemPositionHasChanged && m_problem && m_nodeIndex >= 0 && m_nodeIndex < m_problem->nodes.size()) {
      QPointF center = value.toPointF() + rect().center();
      m_problem->nodes[m_nodeIndex].x = center.x();
      m_problem->nodes[m_nodeIndex].y = center.y();
      if (m_scene)
        m_scene->onNodeMoved(m_nodeIndex);
    }
    return QGraphicsEllipseItem::itemChange(change, value);
  }

  private:
  int m_nodeIndex;
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
  startAngleDeg = std::atan2(y0 - cy, x0 - cx) * 180.0 / M_PI;
  return true;
}

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
}

void GeometryScene::setProblem(FemmProblem* problem)
{
  m_problem = problem;
  rebuild();
}

void GeometryScene::rebuild()
{
  clear();
  m_nodeItems.clear();
  m_segmentItemsByNode.clear();
  m_arcItemsByNode.clear();
  m_pendingSegmentNode = -1;

  if (!m_problem)
    return;

  for (int i = 0; i < m_problem->segments.size(); i++)
    addSegmentItem(i);
  for (int i = 0; i < m_problem->arcSegments.size(); i++)
    addArcItem(i);
  for (int i = 0; i < m_problem->nodes.size(); i++)
    addNodeItem(i);
  for (int i = 0; i < m_problem->blockLabels.size(); i++)
    addBlockLabelItem(i);
}

void GeometryScene::setToolMode(GeometryToolMode mode)
{
  m_toolMode = mode;
  m_pendingSegmentNode = -1;
}

void GeometryScene::addNodeItem(int index)
{
  const FemmNode& n = m_problem->nodes[index];
  QPen pen(Qt::black);
  pen.setCosmetic(true);
  auto* item = new NodeItem(index, m_problem, this,
      QRectF(-kNodeHandlePixelRadius, -kNodeHandlePixelRadius, 2 * kNodeHandlePixelRadius, 2 * kNodeHandlePixelRadius));
  item->setPos(n.x, n.y);
  item->setPen(pen);
  item->setBrush(QBrush(Qt::black));
  addItem(item);
  m_nodeItems[index] = item;
}

void GeometryScene::addSegmentItem(int index)
{
  QPen pen(Qt::blue);
  pen.setCosmetic(true);
  auto* item = addLine(QLineF(), pen);
  item->setFlag(QGraphicsItem::ItemIsSelectable);
  item->setData(KindKey, static_cast<int>(FemmItemKind::Segment));
  item->setData(IndexKey, index);
  updateSegmentItemGeometry(item, index);

  const FemmSegment& s = m_problem->segments[index];
  m_segmentItemsByNode.insert(s.n0, item);
  m_segmentItemsByNode.insert(s.n1, item);
}

void GeometryScene::addArcItem(int index)
{
  QPen pen(QColor(0, 128, 0));
  pen.setCosmetic(true);
  auto* item = addPath(QPainterPath(), pen);
  item->setFlag(QGraphicsItem::ItemIsSelectable);
  item->setData(KindKey, static_cast<int>(FemmItemKind::Arc));
  item->setData(IndexKey, index);
  updateArcItemGeometry(item, index);

  const FemmArcSegment& a = m_problem->arcSegments[index];
  m_arcItemsByNode.insert(a.n0, item);
  m_arcItemsByNode.insert(a.n1, item);
}

void GeometryScene::addBlockLabelItem(int index)
{
  const FemmBlockLabel& b = m_problem->blockLabels[index];
  bool isHole = b.blockTypeIndex < 0;
  double r = kBlockLabelPixelRadius;
  QPen pen(isHole ? QColor(160, 160, 160) : QColor(200, 0, 0));
  pen.setCosmetic(true);

  QPainterPath path;
  path.moveTo(-r, 0);
  path.lineTo(r, 0);
  path.moveTo(0, -r);
  path.lineTo(0, r);
  auto* item = addPath(path, pen);
  item->setFlag(QGraphicsItem::ItemIsMovable);
  item->setFlag(QGraphicsItem::ItemIsSelectable);
  item->setFlag(QGraphicsItem::ItemIgnoresTransformations);
  item->setData(KindKey, static_cast<int>(FemmItemKind::BlockLabel));
  item->setData(IndexKey, index);
  item->setPos(b.x, b.y);
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
    path.arcTo(cx - R, cy - R, 2 * R, 2 * R, startAngleDeg, arc.arcLength);
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

void GeometryScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
  if (!m_problem || event->button() != Qt::LeftButton || m_toolMode == GeometryToolMode::Select) {
    QGraphicsScene::mousePressEvent(event);
    return;
  }
  handleToolClick(event);
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
    QGraphicsScene::mouseDoubleClickEvent(event);
    return;
  }
  handleToolClick(event);
}

void GeometryScene::handleToolClick(QGraphicsSceneMouseEvent* event)
{
  QPointF pos = event->scenePos();

  switch (m_toolMode) {
  case GeometryToolMode::AddNode: {
    int idx = FemmProblemEdit::addNode(*m_problem, pos.x(), pos.y());
    addNodeItem(idx);
    emit problemEdited();
    break;
  }
  case GeometryToolMode::AddBlockLabel: {
    int idx = FemmProblemEdit::addBlockLabel(*m_problem, pos.x(), pos.y());
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
      if (m_pendingSegmentNode < 0) {
        m_pendingSegmentNode = clickedNode;
      } else if (m_pendingSegmentNode != clickedNode) {
        int idx = FemmProblemEdit::addSegment(*m_problem, m_pendingSegmentNode, clickedNode);
        addSegmentItem(idx);
        m_pendingSegmentNode = -1;
        emit problemEdited();
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
  QGraphicsScene::keyPressEvent(event);
}

void GeometryScene::deleteSelectedItem()
{
  if (!m_problem)
    return;
  const auto selected = selectedItems();
  if (selected.isEmpty())
    return;

  // One item per keypress: deleting a node cascades (removes touching
  // segments/arcs) and renumbers every remaining node index, which would
  // invalidate the rest of a stale multi-selection's indices -- rebuild()
  // below re-syncs everything and drops the (now-recreated) selection, so
  // repeated Delete presses handle a multi-selection safely one at a time
  // rather than risking an inconsistent batch edit.
  QGraphicsItem* item = selected.first();
  auto kind = static_cast<FemmItemKind>(item->data(KindKey).toInt());
  int index = item->data(IndexKey).toInt();

  switch (kind) {
  case FemmItemKind::Node: FemmProblemEdit::deleteNode(*m_problem, index); break;
  case FemmItemKind::Segment: FemmProblemEdit::deleteSegment(*m_problem, index); break;
  case FemmItemKind::Arc: FemmProblemEdit::deleteArcSegment(*m_problem, index); break;
  case FemmItemKind::BlockLabel: FemmProblemEdit::deleteBlockLabel(*m_problem, index); break;
  }

  rebuild();
  emit problemEdited();
}
