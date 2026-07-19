#pragma once

#include <QGraphicsItem>
#include <QMainWindow>

#include "MeshSolution.h"

class QGraphicsScene;
class QGraphicsView;

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

  private:
  const MeshSolution* m_solution;
  QRectF m_bounds;
};

class SolutionWindow : public QMainWindow {
  Q_OBJECT

  public:
  explicit SolutionWindow(QWidget* parent = nullptr);

  void openAnsFile(const QString& path);

  private slots:
  void onOpenTriggered();

  private:
  QGraphicsScene* m_scene = nullptr;
  QGraphicsView* m_view = nullptr;
  MeshSolution m_solution;
  MeshSolutionItem* m_item = nullptr;
};
