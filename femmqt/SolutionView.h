#pragma once

#include <QGraphicsItem>
#include <QGraphicsView>
#include <QMainWindow>

#include "MeshSolution.h"

#include <complex>

class QGraphicsScene;
class QAction;

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
  void setSmoothing(bool smooth) { m_smooth = smooth; }

  private:
  void paintDensity(QPainter* painter);
  void paintContour(QPainter* painter);
  void paintVector(QPainter* painter);

  const MeshSolution* m_solution;
  QRectF m_bounds;
  PlotMode m_mode = PlotMode::Density;
  bool m_smooth = true;
};

// Routes plain left-clicks (used by the Point/Contour/Area analysis
// tools) back to SolutionWindow -- QGraphicsView has no built-in "clicked
// at this scene position" signal, and subclassing here is simpler than
// installing an event filter for a single event type.
class SolutionGraphicsView : public QGraphicsView {
  Q_OBJECT

  public:
  explicit SolutionGraphicsView(QGraphicsScene* scene, QWidget* parent = nullptr);

  signals:
  void clickedAt(QPointF scenePos);

  protected:
  void mousePressEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
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
  void onCanvasClicked(QPointF scenePos);
  void onPointToolTriggered();
  void onContourToolTriggered();
  void onAreaToolTriggered();
  void onFinishContourTriggered();
  void onClearContourTriggered();
  void onPlotXYTriggered();
  void onProblemInfoTriggered();
  void onAboutTriggered();

  private:
  // Returns the index of the element containing `pt`, or -1. Linear scan
  // (see the .cpp for why that's an acceptable trade-off here).
  int findContainingElement(QPointF pt) const;
  // Barycentric-interpolates nodal A within element `elementIndex`
  // (assumed to already contain `pt`, i.e. from findContainingElement).

  std::complex<double> interpolateA(QPointF pt, int elementIndex) const;
  void updateContourVisual();

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
};
