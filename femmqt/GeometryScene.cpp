#define _USE_MATH_DEFINES

#include "GeometryScene.h"

#include "AppTheme.h"
#include "FemmProblem.h"
#include "FemmProblemEdit.h"
#include "MeshOverlay.h"

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
    // Fires before the position is actually applied -- snapping here (by
    // returning a modified value) affects the drag itself, not just where
    // it lands, matching how classic FEMM applies grid snap to "the
    // current mouse position" for every interaction, not just placement.
    if (change == ItemPositionChange && m_scene && m_scene->snapToGrid()) {
      QPointF center = value.toPointF() + rect().center();
      return m_scene->snapPoint(center) - rect().center();
    }
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
  BlockLabelItem(const QPainterPath& path, qreal hitRadius)
      : QGraphicsPathItem(path)
      , m_hitRadius(hitRadius)
  {
  }

  QPainterPath shape() const override
  {
    QPainterPath p;
    p.addEllipse(QPointF(0, 0), m_hitRadius, m_hitRadius);
    return p;
  }

  private:
  qreal m_hitRadius;
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
  setBackgroundBrush(AppTheme::background());
}

void GeometryScene::setProblem(FemmProblem* problem)
{
  m_problem = problem;
  rebuild();
}

void GeometryScene::rebuild()
{
  clear(); // deletes every item, including m_zoomWindowRectItem if present
  m_nodeItems.clear();
  m_segmentItemsByNode.clear();
  m_arcItemsByNode.clear();
  m_blockNameItems.clear();
  m_zoomWindowRectItem = nullptr;
  // clear() above already deleted this along with everything else -- an
  // edit invalidates any previous mesh anyway (matches classic FEMM's own
  // MeshUpToDate flag being cleared on any geometry change), so there's no
  // reason to try to preserve/re-add it here.
  m_meshOverlayItem = nullptr;
  m_pendingNode = -1;

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

void GeometryScene::refreshTheme()
{
  setBackgroundBrush(AppTheme::background());
  rebuild();
  update();
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
  auto* item = new NodeItem(index, m_problem, this,
      QRectF(-kNodeHandlePixelRadius, -kNodeHandlePixelRadius, 2 * kNodeHandlePixelRadius, 2 * kNodeHandlePixelRadius));
  item->setPos(n.x, n.y);
  item->setPen(pen);
  item->setBrush(QBrush(AppTheme::nodeColor()));
  addItem(item);
  m_nodeItems[index] = item;
}

void GeometryScene::addSegmentItem(int index)
{
  QPen pen(AppTheme::segmentColor());
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
  QPen pen(AppTheme::arcColor());
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
  QPen pen(isHole ? AppTheme::holeColor() : QColor(200, 0, 0));
  pen.setCosmetic(true);

  QPainterPath path;
  path.moveTo(-r, 0);
  path.lineTo(r, 0);
  path.moveTo(0, -r);
  path.lineTo(0, r);
  auto* item = new BlockLabelItem(path, r);
  item->setPen(pen);
  addItem(item);
  item->setFlag(QGraphicsItem::ItemIsMovable);
  item->setFlag(QGraphicsItem::ItemIsSelectable);
  item->setFlag(QGraphicsItem::ItemIgnoresTransformations);
  // Matches NodeItem's z-value reasoning (see its comment) -- keeps a
  // label clickable/on-top even if it ends up visually over a segment or
  // arc, not exercised by today's bug but the same latent hazard.
  item->setZValue(1.0);
  item->setData(KindKey, static_cast<int>(FemmItemKind::BlockLabel));
  item->setData(IndexKey, index);
  item->setPos(b.x, b.y);

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
  QGraphicsScene::mouseMoveEvent(event);
}

void GeometryScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
  if (m_toolMode == GeometryToolMode::ZoomWindow && m_zoomWindowRectItem && m_zoomWindowRectItem->isVisible()) {
    QRectF r = m_zoomWindowRectItem->rect();
    m_zoomWindowRectItem->setVisible(false);
    if (r.width() > 1e-9 && r.height() > 1e-9)
      emit zoomWindowSelected(r);
    setToolMode(GeometryToolMode::Select); // one-shot, mirrors FemmeView.cpp's ZoomWndFlag reset
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

  switch (m_toolMode) {
  case GeometryToolMode::AddNode: {
    QPointF snapped = snapPoint(pos);
    int idx = FemmProblemEdit::addNode(*m_problem, snapped.x(), snapped.y());
    addNodeItem(idx);
    emit problemEdited();
    break;
  }
  case GeometryToolMode::AddBlockLabel: {
    QPointF snapped = snapPoint(pos);
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

  emit aboutToEdit();
  switch (kind) {
  case FemmItemKind::Node: FemmProblemEdit::deleteNode(*m_problem, index); break;
  case FemmItemKind::Segment: FemmProblemEdit::deleteSegment(*m_problem, index); break;
  case FemmItemKind::Arc: FemmProblemEdit::deleteArcSegment(*m_problem, index); break;
  case FemmItemKind::BlockLabel: FemmProblemEdit::deleteBlockLabel(*m_problem, index); break;
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

bool GeometryScene::selectedEntity(FemmItemKind& kind, int& index) const
{
  const auto sel = selectedItems();
  if (sel.size() != 1)
    return false;
  kind = static_cast<FemmItemKind>(sel.first()->data(KindKey).toInt());
  index = sel.first()->data(IndexKey).toInt();
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

  // One batched path for the whole mesh (matches SolutionView.cpp's
  // MeshSolutionItem reasoning) -- a per-triangle QGraphicsItem doesn't
  // scale to a real mesh's element count.
  QPainterPath path;
  for (const MeshOverlayElement& e : mesh.elements) {
    if (e.p0 < 0 || e.p0 >= mesh.nodes.size() || e.p1 < 0 || e.p1 >= mesh.nodes.size() || e.p2 < 0 || e.p2 >= mesh.nodes.size())
      continue;
    const MeshOverlayNode& a = mesh.nodes[e.p0];
    const MeshOverlayNode& b = mesh.nodes[e.p1];
    const MeshOverlayNode& c = mesh.nodes[e.p2];
    path.moveTo(a.x, a.y);
    path.lineTo(b.x, b.y);
    path.lineTo(c.x, c.y);
    path.lineTo(a.x, a.y);
  }

  QPen pen(AppTheme::meshLineColor());
  pen.setCosmetic(true);
  auto* item = addPath(path, pen);
  item->setZValue(-1.0); // beneath geometry (nodes/segments/arcs/labels)
  item->setVisible(m_showMesh);
  m_meshOverlayItem = item;
}

void GeometryScene::clearMeshOverlay()
{
  if (m_meshOverlayItem) {
    removeItem(m_meshOverlayItem);
    delete m_meshOverlayItem;
    m_meshOverlayItem = nullptr;
  }
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

  QPen pen(AppTheme::gridLine());
  pen.setCosmetic(true);
  painter->setPen(pen);
  for (int i = 0; i < nx; i++) {
    double x = x0 + i * m_gridSize;
    for (int j = 0; j < ny; j++)
      painter->drawPoint(QPointF(x, y0 + j * m_gridSize));
  }
}
