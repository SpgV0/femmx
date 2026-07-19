#pragma once

#include <QGraphicsScene>
#include <QMultiHash>

struct FemmProblem;

enum class FemmItemKind {
  Node = 0,
  Segment = 1,
  Arc = 2,
  BlockLabel = 3,
};

enum class GeometryToolMode {
  Select,
  AddNode,
  AddSegment,
  AddArc,
  AddBlockLabel,
};

// Editable rendering of a FemmProblem's geometry. Holds a non-owning
// pointer to the live problem (owned by MainWindow) rather than a copy --
// edits (drag, add, delete) mutate it in place through FemmProblemEdit,
// immediately reflected on screen, matching a normal CAD-editor feel
// rather than a load/edit-a-copy/re-save round trip.
class GeometryScene : public QGraphicsScene {
  Q_OBJECT

  public:
  explicit GeometryScene(QObject* parent = nullptr);

  void setProblem(FemmProblem* problem);

  // Full rebuild from the current problem state -- clears and re-adds
  // every item. Used after structural edits (add/delete) where an
  // incremental patch isn't worth the bookkeeping; drag-move instead
  // updates the moved node's connected segments/arcs incrementally (see
  // onNodeMoved) so dragging stays smooth.
  void rebuild();

  void setToolMode(GeometryToolMode mode);
  GeometryToolMode toolMode() const { return m_toolMode; }

  // Called by NodeItem::itemChange when the user drags a node -- updates
  // every segment/arc that references it so they follow the drag live.
  void onNodeMoved(int nodeIndex);

  signals:
  void problemEdited();

  // Emitted on a double-click that hits an entity while in Select mode --
  // MainWindow owns the actual per-entity property dialogs (NodePropDialog
  // etc.), so this scene only reports what was clicked rather than
  // depending on those dialog classes itself.
  void entityDoubleClicked(FemmItemKind kind, int index);

  protected:
  void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
  void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

  private:
  void handleToolClick(QGraphicsSceneMouseEvent* event);
  void addNodeItem(int index);
  void addSegmentItem(int index);
  void addArcItem(int index);
  void addBlockLabelItem(int index);
  void updateSegmentItemGeometry(QGraphicsItem* item, int segmentIndex);
  void updateArcItemGeometry(QGraphicsItem* item, int arcIndex);
  void deleteSelectedItem();

  FemmProblem* m_problem = nullptr;
  GeometryToolMode m_toolMode = GeometryToolMode::Select;

  // node index -> item, and node index -> {segment/arc items that touch
  // it}, kept in sync by rebuild() -- used by onNodeMoved to find what
  // needs to follow a drag without a linear scan of every item.
  QHash<int, QGraphicsItem*> m_nodeItems;
  QMultiHash<int, QGraphicsItem*> m_segmentItemsByNode;
  QMultiHash<int, QGraphicsItem*> m_arcItemsByNode;

  int m_pendingNode = -1; // first node clicked while in AddSegment/AddArc mode, -1 if none yet

  // Last-used arc parameters, offered as the default the next time the Add
  // Arc tool prompts for them -- mirrors FemmeView.cpp's MaxSeg/ArcAngle
  // member fields (initialized to the same 1.0/90.0 defaults there).
  double m_lastArcAngleDeg = 90.0;
  double m_lastArcMaxSegDeg = 1.0;
};
