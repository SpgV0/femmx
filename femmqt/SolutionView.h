#pragma once

#include <QElapsedTimer>
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QMainWindow>
#include <QPair>
#include <QVector>

#include "MeshSolution.h"

#include <complex>

class QGraphicsScene;
class QAction;
class QDockWidget;
class QToolBar;
class QPlainTextEdit;

// Paints the whole solved mesh (potentially millions of triangles) in a
// handful of QPainter calls -- one filled QPainterPath per color band,
// batched the same way femm/FemmviewView.cpp's PlotFluxDensity/
// FlushDensityBand batch into PolyPolygon() calls, and for the same
// reason: one draw call per element does not scale to meshes this app is
// meant to handle. A single QGraphicsItem (not one per triangle) so it
// still gets QGraphicsView's pan/zoom/rubber-band plumbing for free.
class MeshSolutionItem : public QGraphicsItem {
  public:
  explicit MeshSolutionItem(const MeshSolution* solution);

  QRectF boundingRect() const override;
  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

  // Density (colored |B| bands, the default), Contour (evenly-spaced
  // equipotential/A-contour lines), or Vector (arrows sampled at element
  // centroids) -- mirrors femm.rc's View > Density/Contour/Vector Plot,
  // though this is a first pass at each rather than the classic dialogs'
  // full configurability (band count, arrow scale, etc.).
  enum class PlotMode { Density, Contour, Vector };
  void setPlotMode(PlotMode mode);
  void setSmoothing(bool smooth);
  void setShowMesh(bool show);
  void setShowPoints(bool show);

  private:
  void paintDensity(QPainter* painter);
  void paintContour(QPainter* painter);
  void paintVector(QPainter* painter);
  void paintMeshOverlay(QPainter* painter);

  const MeshSolution* m_solution;
  QRectF m_bounds;
  PlotMode m_mode = PlotMode::Density;
  bool m_smooth = true;
  bool m_showMesh = false;
  bool m_showPoints = false;

  // Per-node average of touching elements' |B| -- femm/FemmviewView.cpp's
  // "Smooth" option colors each triangle using its 3 corner nodes' values
  // (via GDI's GradientFill) instead of one flat per-element value; Qt's
  // QPainter has no equivalent triangle-gradient primitive, so this
  // approximates it by banding on the *average* of the 3 corner nodes'
  // values instead of the element's own single value -- softens the
  // element-to-element steps at shared edges without needing per-pixel
  // rasterization. Computed once in the constructor.
  QVector<double> m_nodeBMagAvg;
};

// Routes plain left-clicks (used by the Point/Contour/Area analysis
// tools) back to SolutionWindow -- QGraphicsView has no built-in "clicked
// at this scene position" signal, and subclassing here is simpler than
// installing an event filter for a single event type.
class SolutionGraphicsView : public QGraphicsView {
  Q_OBJECT

  public:
  explicit SolutionGraphicsView(QGraphicsScene* scene, QWidget* parent = nullptr);

  // Antialiasing is off by default (large meshes: rasterizing millions of
  // triangles with AA on is real, measurable extra cost) -- but disabled
  // AA leaves faint 1px seams between adjacent triangles at their shared
  // edges, invisible at typical zoom where each triangle is a handful of
  // pixels, but a real, ugly "swiss cheese" artifact once zoomed in far
  // enough that a triangle covers only a few screen pixels (confirmed
  // directly: zooming in on a real solved mesh showed exactly this).
  // Re-enable AA once zoomed in past a threshold where few enough
  // triangles are ever on screen at once that AA's cost stops mattering.
  // Call after any operation that changes the view's scale.
  void updateAntialiasingForScale();

  // Updates the floating cursor-following tooltip's text (position is
  // maintained internally, following the last mouse move) -- called by
  // SolutionWindow::onCanvasHovered once it's computed the field value,
  // which is throttled (see that function's comment), unlike the
  // tooltip's own position tracking below, which isn't.
  void setTooltipText(const QString& text);

  signals:
  void clickedAt(QPointF scenePos);
  // Emitted on every mouse move over the canvas (setMouseTracking is on),
  // for the status bar's live field-value-under-cursor readout -- unlike
  // clickedAt, this isn't gated on the current tool mode; SolutionWindow
  // decides whether/how to use it.
  void hoveredAt(QPointF scenePos);

  protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

  private:
  class QLabel* m_cursorTooltip = nullptr;
};

enum class SolutionToolMode {
  None,
  Point,
  Contour,
  Area,
};

class SolutionWindow : public QMainWindow {
  Q_OBJECT

  public:
  explicit SolutionWindow(QWidget* parent = nullptr);

  void openAnsFile(const QString& path);

  private slots:
  void onOpenTriggered();
  void onReloadTriggered();
  void onCanvasClicked(QPointF scenePos);
  void onCanvasHovered(QPointF scenePos);
  void onPointToolTriggered();
  void onContourToolTriggered();
  void onAreaToolTriggered();
  void onFinishContourTriggered();
  void onClearContourTriggered();
  void onPlotXYTriggered();
  void onIntegrateTriggered();
  void onProblemInfoTriggered();
  void onCircuitPropsTriggered();
  void onBhCurvesTriggered();
  void onZoomIn();
  void onZoomOut();
  void onZoomNatural();
  void onPanLeft();
  void onPanRight();
  void onPanUp();
  void onPanDown();
  void onCopyBitmapTriggered();
  void onPrintTriggered();
  void onPrintPreviewTriggered();
  void onPrintSetupTriggered();
  void onSwitchToClassicTriggered();
  void onOpenRecentFile();
  void onHelpTopicsTriggered();
  void onLicenseTriggered();
  void onAboutTriggered();

  private:
  // Returns the index of the element containing `pt`, or -1. Linear scan
  // (see the .cpp for why that's an acceptable trade-off here).
  int findContainingElement(QPointF pt) const;
  // Barycentric-interpolates nodal A within element `elementIndex`
  // (assumed to already contain `pt`, i.e. from findContainingElement).
  std::complex<double> interpolateA(QPointF pt, int elementIndex) const;
  void updateContourVisual();
  void addToRecentFiles(const QString& path);
  void updateRecentFilesMenu();
  QAction* addThemedAction(class QToolBar* bar, const QString& iconPath, const QString& text, void (SolutionWindow::*slot)());
  void refreshToolbarIcons();
  void showContourIntegral();
  // Echoes a Point/Contour/Area result into the persistent Output Window
  // dock, mirroring femm/FemmviewView.cpp's OutputWindowText/IDC_OUTBOX --
  // classic FEMM keeps the *last* result visible in a docked bar instead
  // of only a popup dialog; this keeps a scrollback of all of them, which
  // is strictly more useful and no harder to implement.
  void appendOutput(const QString& text);

  QGraphicsScene* m_scene = nullptr;
  SolutionGraphicsView* m_view = nullptr;
  MeshSolution m_solution;
  MeshSolutionItem* m_item = nullptr;

  SolutionToolMode m_toolMode = SolutionToolMode::None;
  QAction* m_pointToolAction = nullptr;
  QAction* m_contourToolAction = nullptr;
  QAction* m_areaToolAction = nullptr;

  QVector<QPointF> m_contourPoints;
  QGraphicsItem* m_contourVisual = nullptr;
  QString m_currentPath;
  QMenu* m_recentFilesMenu = nullptr;

  QDockWidget* m_outputDock = nullptr;
  QPlainTextEdit* m_outputText = nullptr;
  class QLabel* m_positionLabel = nullptr;
  QElapsedTimer m_hoverThrottle; // see onCanvasHovered's comment
  class QPrinter* m_printer = nullptr; // lazily created, shared by Print/Print Preview/Print Setup
  QVector<QPair<QAction*, QString>> m_themedActions; // see MainWindow's identically-named member/refreshToolbarIcons for why
};
