#pragma once

#include <QMainWindow>

#include "FemmProblem.h"
#include "GeometryScene.h"
#include "GeometryView.h"

class QAction;
class QLabel;
class QMenu;

class MainWindow : public QMainWindow {
  Q_OBJECT

  public:
  explicit MainWindow(QWidget* parent = nullptr);

  // Opens a .fem file immediately, bypassing the file dialog -- used for
  // the femm.cfg-driven GUI switch (step 7) and for command-line-argument
  // opens, so both paths share one code path.
  void openFile(const QString& path);

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
  void onImportDxfTriggered();
  void onExportDxfTriggered();
  void onPrintTriggered();
  void onPrintPreviewTriggered();
  void onPrintSetupTriggered();
  void onDeleteSelectedTriggered();
  void onOpenSelectedTriggered();
  void onCopyBitmapTriggered();
  void onEntityDoubleClicked(FemmItemKind kind, int index);
  void onZoomIn();
  void onZoomOut();
  void onZoomNatural();
  void onZoomWindowTriggered();
  void onZoomWindowSelected(QRectF sceneRect);
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
  void onCreateMeshTriggered();
  void onPurgeMeshTriggered();
  void onShowOrphansTriggered();
  void onSelectByGroupTriggered();
  void onSetGroupTriggered();
  void onHelpTopicsTriggered();
  void onLicenseTriggered();
  void onAboutTriggered();
  void onOpenRecentFile();
  void onMousePositionChanged(QPointF scenePos);

  private:
  bool saveAs(const QString& path);
  bool confirmDiscardUnsavedChanges();
  void updateTitle();
  bool hasAppliedPeriodicBoundary() const;
  void markEdited();
  void snapshotForUndo();
  void addToRecentFiles(const QString& path);
  void updateRecentFilesMenu();
  void refreshToolbarIcons();

  GeometryScene* m_scene = nullptr;
  GeometryView* m_view = nullptr;
  FemmProblem m_problem;
  QString m_currentPath;
  bool m_dirty = false;

  // Single-level undo, matching classic FEMM's own CFemmeDoc::UpdateUndo/
  // Undo (a snapshot overwritten before each destructive op, not a full
  // undo/redo stack) -- see snapshotForUndo()'s call sites.
  FemmProblem m_undoSnapshot;
  bool m_hasUndo = false;

  QAction* m_selectToolAction = nullptr;
  QAction* m_addNodeToolAction = nullptr;
  QAction* m_addSegmentToolAction = nullptr;
  QAction* m_addArcToolAction = nullptr;
  QAction* m_addBlockLabelToolAction = nullptr;
  QAction* m_showMeshAction = nullptr;
  QMenu* m_recentFilesMenu = nullptr;
  QLabel* m_positionLabel = nullptr;

  class SolutionWindow* m_solutionWindow = nullptr;
  class LoadMonitorDialog* m_loadMonitor = nullptr;
  class QPrinter* m_printer = nullptr; // lazily created, shared by Print/Print Preview/Print Setup so settings persist across them
};
