#pragma once

#include <QGraphicsScene>
#include <QMultiHash>
#include <QVector>

#include "MeshOverlay.h"

struct FemmProblem;
class MeshOverlayItem;

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
  // One-shot: next click-drag defines a rectangle to zoom into, then
  // reverts to Select -- mirrors FemmeView.cpp's OnZoomWnd/ZoomWndFlag.
  ZoomWindow,
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

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21: per
  // user report ("Zoom to fit... does not work") -- root-caused against a
  // real 88k-node model whose true coordinate range was only ~0.03 units
  // wide (a small transformer core, modeled in meters): QGraphicsScene::
  // itemsBoundingRect() came back roughly 1000x too large (45x22 instead
  // of ~0.035x0.066), because NodeItem/BlockLabelItem are flagged
  // ItemIgnoresTransformations (so their marker stays a constant size on
  // screen regardless of zoom) -- Qt has no active view transform to
  // resolve their true screen size against when itemsBoundingRect() is
  // called outside of a paint pass, so each marker's fixed PIXEL size
  // gets treated as scene UNITS instead. That's invisible for models
  // whose real extent is comparable to or larger than a marker's pixel
  // size (every model this session had tested until now), but completely
  // swamps one whose real extent is much smaller -- exactly this file.
  // Bypasses the scene graph entirely: computes bounds straight from the
  // data model (FemmProblem's node/block-label positions, plus each arc's
  // true circle extent so a highly-bulged arc isn't clipped), which has
  // no notion of "pixels" at all and is immune to this class of bug by
  // construction -- also cheaper than walking ~200k QGraphicsItems.
  QRectF computeProblemBounds() const;

  // Re-reads AppTheme's current colors into the scene background and every
  // existing item (via rebuild()) -- called after AppTheme::setDark()
  // toggles, since item colors are baked in at creation time rather than
  // read live from AppTheme on every paint.
  void refreshTheme();

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

  // Grid display/snap -- mirrors FemmeView.cpp's ShowGrid/SnapGrid/
  // GridSize. Snapping applies both to newly-placed nodes/block labels
  // (handleToolClick) and to node drags (NodeItem::itemChange, via
  // snapPoint() below), matching the classic GUI applying it uniformly
  // to "the current mouse position" regardless of what tool is active.
  void setShowGrid(bool show);
  bool showGrid() const { return m_showGrid; }
  void setSnapToGrid(bool snap) { m_snapToGrid = snap; }
  bool snapToGrid() const { return m_snapToGrid; }
  void setGridSize(double size);
  double gridSize() const { return m_gridSize; }
  QPointF snapPoint(QPointF p) const;

  void setShowBlockNames(bool show);
  bool showBlockNames() const { return m_showBlockNames; }

  // Copies the current Qt-side selection (QGraphicsItem::isSelected())
  // into each entity's isSelected field on m_problem -- the contract
  // FemmProblemEdit's move/copy/scale/mirror expect (see their header
  // comment). Called by MainWindow right before invoking any of those.
  void syncSelectionToProblem();
  bool hasSelection() const { return !selectedItems().isEmpty(); }

  // Fills `kind`/`indices` and returns true if the current selection is
  // non-empty and every selected item is the same FemmItemKind -- for
  // femm.rc's "Open Selected" (ID_OPEN_SELECTED), which classic FEMM
  // binds to both the Edit menu and the Space bar and which operates on
  // every currently-selected entity of one kind at once (see
  // CFemmeDoc::OpNodeDlg/OpSegDlg/OpArcSegDlg/OpBlkDlg in femm/
  // FemmeDoc.cpp -- each loops over all IsSelected==TRUE entries, not
  // just one). Returns false (leaving outputs untouched) if nothing is
  // selected or the selection mixes kinds, matching classic's own
  // implicit restriction (its EditAction-mode selection can never mix
  // kinds in the first place). Keeps the QGraphicsItem::data() role
  // encoding this scene uses internally out of MainWindow.
  bool selectedEntities(FemmItemKind& kind, QVector<int>& indices) const;

  // Mesh > Create/Show/Purge Mesh overlay (femm.rc's Mesh menu split --
  // separate from Solve, which meshes internally via SolveRunner::solve
  // without ever touching this overlay).
  void setMeshOverlay(const MeshOverlay& mesh);
  void clearMeshOverlay();
  void setShowMesh(bool show);
  bool showMesh() const { return m_showMesh; }
  bool hasMeshOverlay() const { return m_meshOverlayItem != nullptr; }

  // Selects the nodes (and their attached segments/arcs) that touch
  // exactly one segment/arc -- a "dangling" endpoint that almost always
  // means an unclosed region, a common meshing-failure cause worth
  // surfacing before the user finds out from a cryptic triangle.exe
  // error. Mirrors CFemmeDoc::SelectOrphans (femm/MOVECOPY.CPP:2411).
  // Returns true if any orphans were found (and selected).
  bool selectOrphans();

  // Simplified, instant-action equivalent of femm.rc's "Operation > Group"
  // mode -- see MainWindow's Edit menu comment for why this isn't a
  // persistent tool mode here.
  void selectByGroup(int groupNumber);
  void applyGroupToSelected(int groupNumber);

  // Deletes one selected item (matching femm.rc's "Delete" -- see the
  // .cpp's comment on why this is one-item-per-call, same as the Delete
  // key's own handling in keyPressEvent, which now just forwards here).
  void deleteSelectedItem();

  signals:
  void problemEdited();

  // Emitted right before a destructive edit (currently: Delete) actually
  // mutates m_problem -- MainWindow connects this to push an undo-stack
  // snapshot. Move/Copy/Scale/Mirror instead call MainWindow's
  // snapshotForUndo() directly before invoking FemmProblemEdit, since
  // those are triggered from MainWindow's own menu handlers rather than
  // from inside this scene.
  void aboutToEdit();

  // Emitted on a double-click that hits an entity while in Select mode --
  // MainWindow owns the actual per-entity property dialogs (NodePropDialog
  // etc.), so this scene only reports what was clicked rather than
  // depending on those dialog classes itself.
  void entityDoubleClicked(FemmItemKind kind, int index);

  // Emitted on Space bar -- femm.rc/FemmeView.cpp::OnKeyDown binds
  // VK_SPACE to the same ID_OPEN_SELECTED command as the Edit menu item,
  // so this mirrors that rather than hardcoding the property-dialog logic
  // here (MainWindow owns it, same reasoning as entityDoubleClicked above).
  void openSelectedRequested();

  // Emitted on every mouse move over the canvas, in scene (model)
  // coordinates -- MainWindow shows this in a permanent status bar label
  // (femm.rc's status bar shows the same "current cursor position" while
  // drawing). Emitted with the already-grid-snapped position when
  // snapping is on, matching what a click would actually place.
  void mousePositionChanged(QPointF scenePos);

  // Emitted when a ZoomWindow drag completes -- MainWindow owns the
  // QGraphicsView and does the actual fitInView().
  void zoomWindowSelected(QRectF sceneRect);

  protected:
  void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
  void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
  void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
  void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void drawBackground(QPainter* painter, const QRectF& rect) override;

  private slots:
  // Applies/removes a drop-shadow QGraphicsEffect as items become
  // selected/deselected -- Qt's own default selection decoration (a thin
  // dashed bounding-box outline) was reported as too subtle to notice at
  // a glance. Connected to the base class's own selectionChanged()
  // signal in the constructor.
  void onSelectionChanged();

  private:
  void handleToolClick(QGraphicsSceneMouseEvent* event);
  void addNodeItem(int index);
  void addSegmentItem(int index);
  void addArcItem(int index);
  void addBlockLabelItem(int index);
  void updateSegmentItemGeometry(QGraphicsItem* item, int segmentIndex);
  void updateArcItemGeometry(QGraphicsItem* item, int arcIndex);

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

  bool m_showGrid = true; // matches femm.rc's IDR_FEMMETYPE Show Grid, CHECKED by default
  bool m_snapToGrid = false;
  double m_gridSize = 1.0;

  bool m_showBlockNames = false;
  QHash<int, QGraphicsItem*> m_blockNameItems; // block label index -> its name text item

  QGraphicsRectItem* m_zoomWindowRectItem = nullptr;
  QPointF m_zoomWindowStartPos;

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-22: was
  // a single QGraphicsPathItem built once from the WHOLE mesh -- see
  // MeshOverlayItem's header comment for why that doesn't scale. m_mesh
  // owns the data for m_meshOverlayItem's lifetime (mirrors
  // SolutionWindow::m_solution / MeshSolutionItem).
  MeshOverlay m_mesh;
  MeshOverlayItem* m_meshOverlayItem = nullptr;
  bool m_showMesh = false;

  // Items currently wearing the onSelectionChanged() drop-shadow effect --
  // tracked directly (rather than re-scanning every item's isSelected()
  // each time) so removing a stale effect from a since-deselected item is
  // O(previous selection size), not O(every item in the scene).
  QList<QGraphicsItem*> m_shadowedItems;
};
