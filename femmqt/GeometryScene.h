#pragma once

#include <QGraphicsScene>
#include <QMultiHash>
#include <QVector>

#include "ConstraintSolver.h"
#include "MeshOverlay.h"

struct FemmProblem;
class MeshOverlayItem;
// Forward-declarable as a bare scoped enum (defined in FemmProblem.h with
// no explicit underlying type, so it defaults to int) -- avoids a full
// FemmProblem.h include just to name this type in a member declaration
// below.
enum class DimensionType;

enum class FemmItemKind {
  Node = 0,
  Segment = 1,
  Arc = 2,
  BlockLabel = 3,
  // Modified by Claude (Anthropic), noreply@anthropic.com: CAD-style
  // dimension annotations -- see FemmProblem.h's FemmDimension comment.
  Dimension = 4,
  // Modified by Claude (Anthropic), noreply@anthropic.com: on-canvas
  // constraint glyph -- a small clickable icon marking where a
  // FemmConstraint applies (see FemmProblem.h's FemmConstraint comment),
  // matching Fusion 360's own on-geometry relation markers per direct user
  // request ("symbols indicating constraints than I can click and
  // remove"). Selecting/deleting one goes through the exact same
  // deleteSelectedItem() path as every other kind.
  Constraint = 5,
};

enum class GeometryToolMode {
  Select,
  AddNode,
  AddSegment,
  AddArc,
  AddBlockLabel,
  // Modified by Claude (Anthropic), noreply@anthropic.com: CAD-style
  // dimension placement tools -- click the entities the dimension
  // references (2 nodes for Distance, 1 arc for Radius, 3 nodes --
  // vertex then two ray endpoints -- for Angle), same click-pattern as
  // AddSegment/AddArc, then a QInputDialog::getDouble prefilled with the
  // live-measured value (matching AddArc's own angle-input precedent).
  AddDimensionDistance,
  AddDimensionRadius,
  AddDimensionAngle,
  // Modified by Claude (Anthropic), noreply@anthropic.com: "Smart
  // Dimension" -- per direct user request ("I want to be able to set
  // dimension by pressing D and [click] the line or the nodes"), matching
  // SolidWorks/Fusion 360's own "D" Smart Dimension shortcut, reworked to
  // match that reference's actual select -> cursor-resolved live preview
  // -> placement click -> type-value model (see
  // updateSmartDimensionPreview()/commitSmartDimensionPlacement()'s own
  // comments for the state machine). One click on a SEGMENT or two NODE
  // clicks both start a live Horizontal/Vertical/Aligned distance
  // candidate that follows the cursor; a click on an ARC starts a live
  // Radius candidate; a second click on a segment sharing a vertex with an
  // in-progress segment-length candidate upgrades it to an Angle
  // candidate instead. The NEXT click places the candidate and prompts for
  // its value. See handleToolClick()'s own case for the exact dispatch.
  SmartDimension,
  // Persistent (like the 4 Add* tools above, not one-shot) -- drag between
  // two diagonal corners to place an axis-aligned rectangle: 4 new nodes
  // plus 4 new segments forming a closed loop. No classic FEMM precedent
  // (its own editor only ever builds a rectangle out of manually-placed
  // nodes/segments); a new, deliberate CAD-style convenience per direct
  // user request.
  DrawRectangle,
  // Persistent -- drag from a center point out to the perimeter to place
  // a circle: 2 new nodes (diametrically opposite, on the horizontal axis
  // through the center) plus 2 new 180-degree arc segments connecting
  // them, matching this codebase's own existing convention for a "full
  // circle" (see e.g. test/results/straight_wire_field/
  // straight_wire_field.fem's concentric circles, each built the same
  // way) -- also the direction-insensitive angle (GeometryScene::
  // drawBackground's arc-sweep-sign history), so which way the two nodes
  // end up doesn't matter for correct rendering.
  DrawCircle,
  // One-shot: next click-drag defines a rectangle to zoom into, then
  // reverts to Select -- mirrors FemmeView.cpp's OnZoomWnd/ZoomWndFlag.
  ZoomWindow,
  // One-shot: next click-drag defines a circle (center = press point,
  // radius = drag distance); on release, everything inside it is
  // selected, then reverts to Select -- mirrors FemmeView.cpp's
  // OnFDSelectCirc/SelectCircFlag. Toolbar-only in the classic GUI (no
  // menu item), found missing during a full icon-by-icon toolbar audit.
  SelectCircle,
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

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-26: per
  // user report ("artifacts still shown when dragging a node... cleared
  // away if you pass another window on top or you zoom out") -- node/
  // block-label markers used to stay a constant on-screen size via
  // QGraphicsItem::ItemIgnoresTransformations, but Qt's dirty-rect
  // tracking for that flag is unreliable during rapid position changes
  // (confirmed directly, at length -- see NodeItem::itemChange's own
  // history), leaving stale trails a plain invalidate()/update() fix
  // could only partially clear. Replaced with the same technique already
  // proven for segment/arc hit-testing (see widenedHitShape): markers now
  // use NORMAL scene-space geometry sized from the CURRENT view scale, so
  // Qt's standard (correctly-functioning) per-item dirty tracking applies
  // -- no special-casing needed at all. Since that geometry depends on
  // zoom, GeometryView calls this after every operation that changes its
  // transform (wheel zoom, zoomBy(), fitInViewSafe(), resetZoomTransform())
  // so each marker's local rect/path stays correctly sized.
  void refreshFixedPixelItemSizes();

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

  // Called by BlockLabelItem::itemChange -- keeps the (possibly hidden,
  // see m_showBlockNames) name text item in sync with a dragged label,
  // the same live-follow BlockLabelItem was missing entirely before.
  void onBlockLabelMoved(int index);

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-25: per
  // user report ("the undo button does not work for all the drawing
  // operations") -- NodeItem/BlockLabelItem::itemChange call this once
  // per drag GESTURE (not once per mouse-move frame, which a drag fires
  // many of) right before the first live mutation of m_problem, so Undo
  // can actually restore the pre-drag position. `emit aboutToEdit()`
  // itself is only meant to be called from within this class (see that
  // signal's own comment) -- this wraps it for the two nested item
  // classes in GeometryScene.cpp, which aren't QObjects and can't emit a
  // signal directly, plus the "only once per gesture" bookkeeping they'd
  // otherwise have to duplicate.
  void snapshotOnceForDrag();

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-26: per
  // user report ("draw 5 points, need to undo more than 5 times") --
  // addNodeItem()/addBlockLabelItem() call setPos() to give a freshly
  // created item its initial position, which fires the exact same
  // itemChange(ItemPositionHasChanged) path as a real user drag, so every
  // single Add Node/Add Block Label click was pushing TWO undo snapshots
  // (one explicit, from handleToolClick's aboutToEdit(), plus one spurious
  // one from the new item's own creation-time setPos()). Worse, rebuild()
  // (called on file open, New, AND every Undo) does the same for every
  // node/label in the problem, so undoing could silently inject a phantom
  // extra snapshot into its own stack. addNodeItem()/addBlockLabelItem()
  // set this true around their setPos() call; NodeItem/BlockLabelItem::
  // itemChange skip snapshotting (and the redundant data/segment resync --
  // the position they're being set to already came from m_problem, so
  // there's nothing to sync) while it's set.
  bool isSettingInitialItemPosition() const { return m_settingInitialItemPosition; }

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

  // Modified by Claude (Anthropic), noreply@anthropic.com: generalization
  // of selectedEntities() above that does NOT require a single kind --
  // partitions the current selection into all 4 kinds at once. Used by
  // MainWindow's Constraints menu handlers, several of which need a
  // specific MIX of kinds (e.g. Symmetric needs 2 nodes + 1 segment),
  // not just one homogeneous kind.
  void selectedByKind(QVector<int>& nodes, QVector<int>& segments, QVector<int>& arcs, QVector<int>& blockLabels) const;

  // Modified by Claude (Anthropic), noreply@anthropic.com: stores the
  // per-node sketch-health classification from the last
  // ConstraintSolver::solve() call (FullyConstrained/UnderConstrained/
  // Redundant/Conflicting -- see ConstraintSolver.h), consulted by
  // addNodeItem/addSegmentItem/addArcItem to color constrained geometry
  // accordingly (matching SolidWorks/FreeCAD convention). Persisted as a
  // member (not a one-off repaint) so it survives rebuild() (called
  // after undo, file open, etc.) without the caller needing to re-apply
  // it every time.
  void setConstraintStatus(const QHash<int, ConstraintSolver::SketchStatus>& nodeStatus);

  // Modified by Claude (Anthropic), noreply@anthropic.com: selects
  // constraint `index`'s on-canvas glyph (clearing any prior selection
  // first) -- used by ConstraintListDialog's click-to-highlight, per
  // direct user request ("a list... that you can select on the side").
  // No-op if that index has no glyph (out of range).
  void selectConstraintGlyph(int index);

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

  // Selects every node/block-label whose point falls within the given
  // circle, and every segment/arc whose BOTH endpoint nodes do -- exact
  // match to femm/FemmeView.cpp's OnLButtonUp SelectCircFlag==2 branch
  // (its EditAction==4 case, since this app's Select mode isn't
  // restricted to one entity type at a time the way classic's Node/
  // Segment/Arc/Block toolbar modes are). Returns true if anything was
  // selected. Used by GeometryToolMode::SelectCircle's one-shot drag.
  bool selectByCircle(QPointF center, double radius);

  // Deletes one selected item (matching femm.rc's "Delete" -- see the
  // .cpp's comment on why this is one-item-per-call, same as the Delete
  // key's own handling in keyPressEvent, which now just forwards here).
  void deleteSelectedItem();

  signals:
  void problemEdited();

  // Emitted right before an edit originating IN THIS SCENE actually
  // mutates m_problem -- MainWindow connects this to push an undo-stack
  // snapshot. Covers Delete (deleteSelectedItem), the 4 add-geometry
  // tools (handleToolClick), and node/block-label drags (via
  // snapshotOnceForDrag(), which the two nested QGraphicsItem subclasses
  // in the .cpp call from itemChange since they aren't QObjects and can't
  // emit a signal directly). Modified by Claude (Anthropic),
  // noreply@anthropic.com, 2026-07-25: per user report ("undo does not
  // work for all the drawing operations") -- previously ONLY Delete
  // emitted this, silently leaving every drawing/drag operation
  // unrecoverable by Undo. Move/Copy/Scale/Mirror/Create Radius/Create
  // Open Boundary/Import DXF instead call MainWindow's snapshotForUndo()
  // directly before invoking FemmProblemEdit, since those are triggered
  // from MainWindow's own menu handlers rather than from inside this
  // scene -- unaffected by, and unrelated to, this round's fix.
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

  // Emitted when a SelectCircle drag completes (the actual selection has
  // already happened by this point, see selectByCircle) -- MainWindow
  // uses this only to re-check its Select toolbar button, same reason
  // onZoomWindowSelected does: the one-shot mode reverts the SCENE back
  // to Select internally, but whichever Draw-toolbar QAction was checked
  // before arming this tool (if not already Select) has no other way to
  // find out it should un-check itself.
  void selectByCircleCompleted();

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
  // Modified by Claude (Anthropic), noreply@anthropic.com: shared by
  // AddDimensionDistance/AddDimensionRadius's own handleToolClick cases
  // AND the new SmartDimension tool -- prompts for the value
  // (QInputDialog::getDouble, prefilled with the live-measured value,
  // matching the exact precedent both tools already established), then
  // appends the dimension, solves, and rebuilds. Kept here (not inlined
  // at each call site) so the two tools can never drift out of sync.
  void addDistanceDimensionForNodes(int n0, int n1);
  void addRadiusDimensionForArc(int arcIndex);
  // Modified by Claude (Anthropic), noreply@anthropic.com: Smart
  // Dimension's own select -> move -> preview -> place -> value state
  // machine -- per direct user request to implement femmqt's "D" tool
  // "according to" a supplied Fusion 360 Sketch Dimension reference. That
  // reference's central point (repeated across its own Sections 2-4,
  // 25-29, 33-34) is that dimension creation is NOT one click: selecting
  // geometry only determines what CAN be measured; the cursor position
  // during a subsequent free MOVE (no button held) resolves which
  // measurement is intended (most visibly for two points: Horizontal vs.
  // Vertical vs. Aligned distance -- see updateSmartDimensionPreview's own
  // comment for the exact heuristic); a second click places the preview
  // and only THEN prompts for the value. This replaces SmartDimension's
  // previous "click entity -> immediately prompt" behavior (which is
  // still exactly how the OLD explicit AddDimensionDistance/Radius/Angle
  // tools work, deliberately left unchanged -- see their own
  // handleToolClick cases).
  //
  // m_smartDimAwaitingPlacement is the state flag: false = still
  // selecting entities (handleToolClick's SmartDimension case owns this
  // phase); true = a candidate exists and is following the mouse
  // (mouseMoveEvent forwards to updateSmartDimensionPreview(), and the
  // NEXT click either extends the candidate -- a second line sharing a
  // vertex with the first, upgrading Length to Angle -- or commits it via
  // commitSmartDimensionPlacement()).
  void updateSmartDimensionPreview(QPointF scenePos);
  void commitSmartDimensionPlacement(QPointF scenePos);
  // Discards any in-progress candidate and its preview item -- called on
  // every tool-mode switch (via setToolMode(), matching how
  // m_pendingDimensionNodes is already unconditionally cleared there) and
  // on Escape (matching the reference doc's own implicit "you can back
  // out before the placement click" assumption).
  void cancelSmartDimensionPreview();
  void addNodeItem(int index);
  void addSegmentItem(int index);
  void addArcItem(int index);
  void addBlockLabelItem(int index);
  void addDimensionItem(int index);
  void addConstraintItem(int index);
  QColor constraintStatusColor(int nodeIndex) const;
  void updateSegmentItemGeometry(QGraphicsItem* item, int segmentIndex);
  void updateArcItemGeometry(QGraphicsItem* item, int arcIndex);
  void resetViewBackgroundCache();

  FemmProblem* m_problem = nullptr;
  GeometryToolMode m_toolMode = GeometryToolMode::Select;

  // node index -> item, and node index -> {segment/arc items that touch
  // it}, kept in sync by rebuild() -- used by onNodeMoved to find what
  // needs to follow a drag without a linear scan of every item.
  QHash<int, QGraphicsItem*> m_nodeItems;
  QMultiHash<int, QGraphicsItem*> m_segmentItemsByNode;
  QMultiHash<int, QGraphicsItem*> m_arcItemsByNode;

  // block label index -> its crosshair marker item -- separate from
  // m_blockNameItems below (that's the adjacent text label). Needed so
  // refreshFixedPixelItemSizes() can find every marker to resize on zoom,
  // the same reason m_nodeItems exists.
  QHash<int, QGraphicsItem*> m_blockLabelItems;

  // dimension index -> its item -- same "find every one to rebuild/
  // resize" role as m_blockLabelItems above.
  QHash<int, QGraphicsItem*> m_dimensionItems;
  // node index -> {dimension items referencing it} -- same live-follow
  // role as m_segmentItemsByNode/m_arcItemsByNode, consulted by
  // onNodeMoved.
  QMultiHash<int, QGraphicsItem*> m_dimensionItemsByNode;

  // constraint index -> its glyph item, and node index -> {glyph items
  // whose constraint touches it} -- same two-hash "find every one to
  // rebuild/resize" + "find what follows a drag" roles as
  // m_dimensionItems/m_dimensionItemsByNode above.
  QHash<int, QGraphicsItem*> m_constraintItems;
  QMultiHash<int, QGraphicsItem*> m_constraintItemsByNode;

  int m_pendingNode = -1; // first node clicked while in AddSegment/AddArc mode, -1 if none yet

  // Nodes clicked so far while an AddDimensionXxx tool is active (2 for
  // Distance, 1 for Radius -- actually an arc click, see handleToolClick
  // -- 3 for Angle: vertex, then two ray endpoints), cleared once the
  // dimension is committed or the tool mode changes.
  QVector<int> m_pendingDimensionNodes;
  int m_pendingDimensionArc = -1; // Radius tool's single arc click

  // Smart Dimension's own state -- see updateSmartDimensionPreview()/
  // commitSmartDimensionPlacement()/cancelSmartDimensionPreview()'s
  // declarations above for the overall state machine. m_smartDimType/
  // RefA/RefB/RefC mirror FemmDimension's own fields exactly (same
  // meaning per type -- see FemmProblem.h's FemmDimension comment) since
  // they become that struct's fields verbatim on commit. For a two-point
  // candidate, m_smartDimType is live-updated on every mouse move (see
  // updateSmartDimensionPreview()) to whichever of Distance/
  // HorizontalDistance/VerticalDistance the current cursor position
  // implies, then whatever it is AT the placement click is what commits.
  bool m_smartDimAwaitingPlacement = false;
  // Modified by Claude (Anthropic), noreply@anthropic.com: per direct user
  // request ("the linear dimensions, I want them either vertical,
  // horizontal, or perpendicular to the line being dimensioned") -- true
  // for any
  // candidate whose refA/refB are a plain point pair eligible for live
  // Horizontal/Vertical/Aligned re-resolution (see
  // updateSmartDimensionPreview()'s own comment for the heuristic): both a
  // 2-node-click candidate AND, now, a single-segment-length candidate
  // (refA/refB = that segment's own two endpoints -- Aligned between them
  // IS the segment's true length, so this is a strict superset of the old
  // "locked to Distance" behavior, not a different computation). False for
  // Radius/Angle, neither of which changes TYPE as the mouse moves, only
  // their offset/direction. Deliberately independent of
  // m_smartDimAngleEligible below -- a segment-length candidate can be
  // live-resolved AND still upgrade to Angle on a second connected-segment
  // click; whichever of Distance/Horizontal/Vertical the cursor happened to
  // be showing at that moment is simply discarded in favor of Angle.
  bool m_smartDimTwoPointMode = false;
  // true only for a candidate built from a single SEGMENT click (so its
  // refA/refB endpoints unambiguously form a real line) -- gates whether a
  // second click on a different segment sharing a vertex with it upgrades
  // the candidate to an Angle dimension (handleToolClick's Stage 2). False
  // for a 2-node-click candidate (no guarantee those two nodes are even
  // connected by a real segment) or Radius/Angle.
  bool m_smartDimAngleEligible = false;
  DimensionType m_smartDimType;
  int m_smartDimRefA = -1, m_smartDimRefB = -1, m_smartDimRefC = -1;
  QGraphicsItem* m_smartDimPreviewItem = nullptr;
  QGraphicsItem* m_smartDimPreviewText = nullptr;

  // Per-node sketch-health classification from the last
  // ConstraintSolver::solve() -- see setConstraintStatus()'s own comment.
  QHash<int, ConstraintSolver::SketchStatus> m_constraintNodeStatus;

  // Last-used arc parameters, offered as the default the next time the Add
  // Arc tool prompts for them -- mirrors FemmeView.cpp's MaxSeg/ArcAngle
  // member fields (initialized to the same 1.0/90.0 defaults there).
  double m_lastArcAngleDeg = 90.0;
  double m_lastArcMaxSegDeg = 1.0;

  // Modified by Claude (Anthropic), noreply@anthropic.com: was `true`
  // (matching femm.rc's IDR_FEMMETYPE Show Grid, checked by default) --
  // per direct user request, femmqt now defaults Show Grid to OFF. Menu/
  // toolbar checked-state is seeded from this same member (MainWindow's
  // View > Grid > Show Grid and the Navigate toolbar button), so this one
  // change covers both.
  bool m_showGrid = false;
  bool m_snapToGrid = false;
  double m_gridSize = 1.0;

  bool m_showBlockNames = false;
  QHash<int, QGraphicsItem*> m_blockNameItems; // block label index -> its name text item

  QGraphicsRectItem* m_zoomWindowRectItem = nullptr;
  QPointF m_zoomWindowStartPos;

  QGraphicsEllipseItem* m_selectCircleItem = nullptr;
  QPointF m_selectCircleStartPos;

  // Rubber-band previews for DrawRectangle/DrawCircle -- same dashed-
  // preview-item pattern as m_zoomWindowRectItem/m_selectCircleItem above,
  // just committing real geometry into m_problem on release instead of
  // zooming/selecting.
  QGraphicsRectItem* m_drawRectItem = nullptr;
  QPointF m_drawRectStartPos;

  QGraphicsEllipseItem* m_drawCircleItem = nullptr;
  QPointF m_drawCircleStartPos;

  // See snapshotOnceForDrag()'s own comment -- reset on every mouse
  // release so the NEXT drag gesture gets its own single snapshot.
  bool m_dragSnapshotTaken = false;

  // See isSettingInitialItemPosition()'s own comment.
  bool m_settingInitialItemPosition = false;

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
