#pragma once

#include <QGraphicsItem>
#include <QVector>

#include "MeshOverlay.h"

// Paints the geometry editor's "Show Mesh" overlay (triangle.exe's raw
// pre-solve .node/.ele output, potentially millions of elements).
// Mirrors SolutionView.h's MeshSolutionItem's design exactly -- a
// uniform-grid spatial index built once, then paint() only ever builds a
// QPainterPath for the elements whose bounding box overlaps the current
// exposedRect, instead of the whole mesh -- see that class's header
// comment for the full rationale.
//
// Previously this overlay was a single QGraphicsPathItem built once from
// EVERY element regardless of what's ever on screen. That doesn't scale
// to a multi-million-element mesh at deep zoom: confirmed directly on a
// real ~3M-element model that mesh edges (and the domain boundary) drew
// visibly, progressively thicker than their cosmetic pen should ever
// produce as zoom increased -- Qt's path-transform/stroke pipeline
// degrading under a single huge path pushed through an extreme view
// transform, not an actual pen-width bug. Since the path already only
// ever needs to represent what's on screen, culling it exactly the way
// the Solution Viewer's mesh already does removes the huge-path
// condition entirely rather than working around its symptom.
class MeshOverlayItem : public QGraphicsItem {
  public:
  explicit MeshOverlayItem(const MeshOverlay* mesh);

  QRectF boundingRect() const override;
  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

  private:
  struct SpatialIndex {
    double minX = 0, minY = 0, cellSize = 1;
    int cols = 0, rows = 0;
    QVector<QVector<int>> cells;
  };
  void buildSpatialIndex();
  QVector<int> elementsOverlapping(const QRectF& rect) const;

  const MeshOverlay* m_mesh;
  QRectF m_bounds;
  SpatialIndex m_spatialIndex;
  mutable QVector<int> m_visitedMark;
  mutable int m_visitedGen = 0;
};
