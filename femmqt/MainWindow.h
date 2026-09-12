#pragma once

#include <QImage>
#include <QMainWindow>
#include <QPair>
#include <QVector>

#include "FemmProblem.h"
#include "ConstructionGeometry.h"
#include "GeometryScene.h"
#include "SketchTransform.h"
#include "GeometryView.h"

class QAction;
class QLabel;
class QMenu;
class QToolBar;

class MainWindow : public QMainWindow {
  Q_OBJECT

  public:
  explicit MainWindow(QWidget* parent = nullptr);

  // Opens a .fem file immediately, bypassing the file dialog -- used for
  // the femm.cfg-driven GUI switch (step 7) and for command-line-argument
  // opens, so both paths share one code path.
  void openFile(const QString& path);

  // Renders the loaded geometry offscreen to an image, for the
  // `femmqt.exe --render-png` CLI mode that Lua's mi_savepng/mo_savepng
  // shell out to when a script has called setgui("qt"). Draws the SCENE
  // rather than grabbing the view: the viewport is a QOpenGLWidget when
  // built with OpenGL, and grabbing one that was never shown is not
  // reliable. Returns a null image if nothing is loaded.
  QImage renderToImage(QSize size, QRectF source = QRectF());


  protected:
  void closeEvent(QCloseEvent* event) override;

  private slots:
  void onNewTriggered();
  void onOpenTriggered();
  void onSaveTriggered();
  void onSaveAsTriggered();
  void onSolveTriggered();
  void onViewResultsTriggered();
  void onSwitchToClassicTriggered();
  void onProblemEdited();
  void onProblemPropertiesTriggered();
  void onMaterialsTriggered();
  void onBoundaryPropsTriggered();
  void onCircuitsTriggered();
  void onPointPropsTriggered();
  void onExteriorRegionTriggered();
  void onMaterialsLibraryTriggered();
  void onPreferencesTriggered();
  void onDarkThemeToggled(bool dark);
  void onLoadMonitorToggled(bool show);
  void onCreateOpenBoundaryTriggered();
  void onCreateRadiusTriggered();
  void onChamferTriggered();
  void onOffsetTriggered();
  // Construction geometry (#31).
  void onAddCentrelineTriggered();
  void onAddBoltCircleTriggered();
  void onAddReferenceRectangleTriggered();
  void onImportDxfTriggered();
  void onExportDxfTriggered();
  void onPrintTriggered();
  void onPrintPreviewTriggered();
  void onPrintSetupTriggered();
  void onDeleteSelectedTriggered();
  void onSelectByCircleTriggered();
  void onOpenSelectedTriggered();
  void onCopyBitmapTriggered();
  void onEntityDoubleClicked(FemmItemKind kind, int index);
  void onZoomIn();
  void onZoomOut();
  void onZoomNatural();
  void onZoomWindowTriggered();
  void onZoomWindowSelected(QRectF sceneRect);
  void onKbdZoomTriggered();
  void onPanLeft();
  void onPanRight();
  void onPanUp();
  void onPanDown();
  void onSetGridTriggered();
  void onUndoTriggered();
  void onMoveSelectedTriggered();
  void onCopySelectedTriggered();
  void onScaleSelectedTriggered();
  void onMirrorSelectedTriggered();
  // Modified by Claude (Anthropic), noreply@anthropic.com: CAD-style
  // geometric constraints -- per direct user request ("add dimensions
  // when drawings and constraints similar to modern cad"). Each
  // validates the current selection's shape (right count/kind of
  // entities for that constraint type) via GeometryScene::selectedByKind,
  // then routes through applyConstraint() -- the same snapshotForUndo->
  // mutate->solve->rebuild->markEdited shape onMoveSelectedTriggered()
  // etc. already use, just with an added solve step. See ConstraintSolver.h
  // for the solver itself and FemmProblem.h's FemmConstraint comment for
  // why these are in-session only, never saved to .fem/.femx.
  void onCoincidentConstraintTriggered();
  void onHorizontalConstraintTriggered();
  void onVerticalConstraintTriggered();
  void onParallelConstraintTriggered();
  void onPerpendicularConstraintTriggered();
  void onEqualConstraintTriggered();
  void onTangentConstraintTriggered();
  void onConcentricConstraintTriggered();
  void onSymmetricConstraintTriggered();
  // Forces a re-solve of every current constraint/dimension -- useful
  // after a batch of edits, or just to double-check sketch health.
  void onSolveConstraintsTriggered();
  void onClearConstraintsTriggered();
  void onClearDimensionsTriggered();
  // Modified by Claude (Anthropic), noreply@anthropic.com: opens a
  // relations-panel-style list of every current constraint (Fusion 360's
  // own "Sketch Palette" list), selectable (highlights that constraint's
  // on-canvas glyph -- see GeometryScene::selectConstraintGlyph) and
  // individually deletable -- per direct user request ("make a list in a
  // dialog with all constraints that you can select on the side"). Fills
  // the gap the module's own scope notes previously flagged: "no
  // on-canvas glyph to select one" is no longer true either, now that
  // ConstraintGlyphItem exists (see GeometryScene.cpp), but this dialog
  // is the more discoverable/bulk-friendly way to browse and prune a
  // sketch with many constraints.
  void onConstraintListTriggered();
  void onCreateMeshTriggered();
  void onPurgeMeshTriggered();
  void onShowOrphansTriggered();
  void onSelectByGroupTriggered();
  void onSetGroupTriggered();
  void onHelpTopicsTriggered();
  void onKeyboardShortcutsTriggered();
  void onLicenseTriggered();
  void onAboutTriggered();
  void onOpenRecentFile();
  void onMousePositionChanged(QPointF scenePos);
  void onSnapChanged(const SnapEngine::SnapResult& snap);
  // Matches femm/FemmeView.cpp's EnterPoint() -- TAB while Add Node/Add
  // Block Label is active (see GeometryView::enterPointRequested), types
  // an exact coordinate instead of clicking one on the canvas.
  void onEnterPointTriggered();

  private:
  bool saveAs(const QString& path);
  bool confirmDiscardUnsavedChanges();
  void updateTitle();
  bool hasAppliedPeriodicBoundary() const;
  void markEdited();
  void snapshotForUndo();
  // Shared by onEntityDoubleClicked (a single-item selection) and
  // onOpenSelectedTriggered (whatever's currently selected, possibly
  // more than one item of the same kind) -- constructs the right
  // PropDialog with pointers into m_problem's own storage for `indices`
  // and applies it. Matches femm.rc's ID_OPEN_SELECTED semantics -- see
  // GeometryScene::selectedEntities' comment.
  void openEntityProperties(FemmItemKind kind, const QVector<int>& indices);
  // Shared tail for every onXxxConstraintTriggered() handler -- see their
  // own comment. Appends `c` to m_problem.constraints, solves, and
  // reflects the result (including a status-bar note if it couldn't
  // fully converge -- a real, expected outcome for a conflicting
  // constraint, not something to silently swallow).
  void applyConstraint(const FemmConstraint& c);
  void addToRecentFiles(const QString& path);
  void updateRecentFilesMenu();
  void refreshToolbarIcons();
  // Adds a toolbar action wired to `slot`, remembers its icon path so
  // refreshToolbarIcons() can re-tint it after a theme change, and sets
  // an explicit tooltip (shown after a 2-second hover via HoverTooltip,
  // installed separately once a toolbar's buttons are all in place).
  // #31. Not slots: called from the Construction Geometry submenu's own
  // lambdas, which supply the direction.
  void reportSketchTransform(const QString& title, const SketchTransform::Report& r);
  void convertSelectionConstruction(bool toConstruction);
  void applyConstructionResult(const QString& title,
      const ConstructionGeometry::Result& r);

  QAction* addThemedAction(QToolBar* bar, const QString& iconPath, const QString& text, const QString& tooltip, void (MainWindow::*slot)());

  GeometryScene* m_scene = nullptr;
  GeometryView* m_view = nullptr;
  FemmProblem m_problem;
  QString m_currentPath;
  bool m_dirty = false;
  // Updated by onMousePositionChanged; used as onEnterPointTriggered's
  // starting point, matching femm/FemmeView.cpp's EnterPoint() defaulting
  // to the cursor's current (mx, my).
  QPointF m_lastMousePos;

  // A bounded undo stack (up to kMaxUndoSteps snapshots), unlike classic
  // FEMM's own CFemmeDoc::UpdateUndo/Undo (a single overwritten snapshot)
  // -- see snapshotForUndo()'s call sites for what's covered. No redo:
  // not requested, and classic FEMM doesn't have one either.
  static constexpr int kMaxUndoSteps = 20;
  QList<FemmProblem> m_undoStack;

  QAction* m_selectToolAction = nullptr;
  QAction* m_addNodeToolAction = nullptr;
  QAction* m_addSegmentToolAction = nullptr;
  QAction* m_addArcToolAction = nullptr;
  QAction* m_addBlockLabelToolAction = nullptr;
  QAction* m_addRectangleToolAction = nullptr;
  QAction* m_addCircleToolAction = nullptr;
  // Trim/Extend/Split (#29) -- the three tools that modify existing
  // geometry rather than adding or deleting it.
  QAction* m_trimToolAction = nullptr;
  QAction* m_extendToolAction = nullptr;
  QAction* m_splitToolAction = nullptr;
  QAction* m_addDimensionDistanceToolAction = nullptr;
  QAction* m_addDimensionRadiusToolAction = nullptr;
  QAction* m_addDimensionAngleToolAction = nullptr;
  QAction* m_smartDimensionToolAction = nullptr;
  QAction* m_showMeshAction = nullptr;
  QMenu* m_recentFilesMenu = nullptr;
  QLabel* m_positionLabel = nullptr;
  // Names the active object snap (#28); empty when nothing is snapped.
  QLabel* m_snapLabel = nullptr;
  // Every toolbar action added via addThemedAction(), paired with the SVG
  // path it was built from -- refreshToolbarIcons() walks this to re-tint
  // all of them after a dark/light toggle, not just the original 5 draw
  // tools (m_selectToolAction etc., which stay separate members since
  // they're also referenced elsewhere for their checked state).
  QVector<QPair<QAction*, QString>> m_themedActions;

  class SolutionWindow* m_solutionWindow = nullptr;
  class LoadMonitorDialog* m_loadMonitor = nullptr;
  class QPrinter* m_printer = nullptr; // lazily created, shared by Print/Print Preview/Print Setup so settings persist across them
};
