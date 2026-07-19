#pragma once

#include <QMainWindow>

#include "FemmProblem.h"
#include "GeometryScene.h"

class QGraphicsView;
class QAction;

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
  void onEntityDoubleClicked(FemmItemKind kind, int index);

  private:
  bool saveAs(const QString& path);
  bool confirmDiscardUnsavedChanges();
  void updateTitle();
  bool hasAppliedPeriodicBoundary() const;
  void markEdited();

  GeometryScene* m_scene = nullptr;
  QGraphicsView* m_view = nullptr;
  FemmProblem m_problem;
  QString m_currentPath;
  bool m_dirty = false;

  QAction* m_selectToolAction = nullptr;
  QAction* m_addNodeToolAction = nullptr;
  QAction* m_addSegmentToolAction = nullptr;
  QAction* m_addArcToolAction = nullptr;
  QAction* m_addBlockLabelToolAction = nullptr;

  class SolutionWindow* m_solutionWindow = nullptr;
};
