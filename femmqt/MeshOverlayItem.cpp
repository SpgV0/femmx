#include "MeshOverlayItem.h"

#include "AppTheme.h"

#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionGraphicsItem>

#include <algorithm>
#include <cmath>

MeshOverlayItem::MeshOverlayItem(const MeshOverlay* mesh)
    : m_mesh(mesh)
{
  double xmin = 0, xmax = 0, ymin = 0, ymax = 0;
  bool first = true;
  for (const MeshOverlayNode& n : mesh->nodes) {
    if (first) {
      xmin = xmax = n.x;
      ymin = ymax = n.y;
      first = false;
    } else {
      xmin = std::min(xmin, n.x);
      xmax = std::max(xmax, n.x);
      ymin = std::min(ymin, n.y);
      ymax = std::max(ymax, n.y);
    }
  }
  m_bounds = QRectF(QPointF(xmin, ymin), QPointF(xmax, ymax));
  buildSpatialIndex();
}

QRectF MeshOverlayItem::boundingRect() const
{
  return m_bounds;
}

void MeshOverlayItem::buildSpatialIndex()
{
  m_spatialIndex = SpatialIndex();
  if (m_mesh->elements.isEmpty() || m_mesh->nodes.isEmpty())
    return;

  double w = std::max(m_bounds.width(), 1e-12);
  double h = std::max(m_bounds.height(), 1e-12);
  // Aim for roughly one element per cell on average, same heuristic as
  // MeshSolutionItem::buildSpatialIndex.
  double cellSize = std::sqrt((w * h) / std::max(1, (int)m_mesh->elements.size()));
  if (!(cellSize > 0))
    cellSize = std::max(w, h);
  int cols = std::max(1, (int)(w / cellSize) + 1);
  int rows = std::max(1, (int)(h / cellSize) + 1);

  m_spatialIndex.minX = m_bounds.left();
  m_spatialIndex.minY = m_bounds.top();
  m_spatialIndex.cellSize = cellSize;
  m_spatialIndex.cols = cols;
  m_spatialIndex.rows = rows;
  m_spatialIndex.cells.resize(cols * rows);

  for (int i = 0; i < m_mesh->elements.size(); i++) {
    const MeshOverlayElement& e = m_mesh->elements[i];
    if (e.p0 < 0 || e.p0 >= m_mesh->nodes.size() || e.p1 < 0 || e.p1 >= m_mesh->nodes.size() || e.p2 < 0 || e.p2 >= m_mesh->nodes.size())
      continue;
    const MeshOverlayNode& n0 = m_mesh->nodes[e.p0];
    const MeshOverlayNode& n1 = m_mesh->nodes[e.p1];
    const MeshOverlayNode& n2 = m_mesh->nodes[e.p2];
    double triMinX = std::min({ n0.x, n1.x, n2.x });
    double triMaxX = std::max({ n0.x, n1.x, n2.x });
    double triMinY = std::min({ n0.y, n1.y, n2.y });
    double triMaxY = std::max({ n0.y, n1.y, n2.y });
    int c0 = std::clamp((int)std::floor((triMinX - m_spatialIndex.minX) / cellSize), 0, cols - 1);
    int c1 = std::clamp((int)std::floor((triMaxX - m_spatialIndex.minX) / cellSize), 0, cols - 1);
    int r0 = std::clamp((int)std::floor((triMinY - m_spatialIndex.minY) / cellSize), 0, rows - 1);
    int r1 = std::clamp((int)std::floor((triMaxY - m_spatialIndex.minY) / cellSize), 0, rows - 1);
    for (int r = r0; r <= r1; r++)
      for (int c = c0; c <= c1; c++)
        m_spatialIndex.cells[r * cols + c].push_back(i);
  }

  m_visitedMark.fill(-1, m_mesh->elements.size());
  m_visitedGen = 0;
}

QVector<int> MeshOverlayItem::elementsOverlapping(const QRectF& rect) const
{
  QVector<int> result;
  if (m_spatialIndex.cols == 0 || m_spatialIndex.rows == 0)
    return result;
  int cols = m_spatialIndex.cols, rows = m_spatialIndex.rows;
  double cellSize = m_spatialIndex.cellSize;
  int c0 = std::clamp((int)std::floor((rect.left() - m_spatialIndex.minX) / cellSize), 0, cols - 1);
  int c1 = std::clamp((int)std::floor((rect.right() - m_spatialIndex.minX) / cellSize), 0, cols - 1);
  int r0 = std::clamp((int)std::floor((rect.top() - m_spatialIndex.minY) / cellSize), 0, rows - 1);
  int r1 = std::clamp((int)std::floor((rect.bottom() - m_spatialIndex.minY) / cellSize), 0, rows - 1);
  m_visitedGen++;
  for (int r = r0; r <= r1; r++) {
    for (int c = c0; c <= c1; c++) {
      for (int idx : m_spatialIndex.cells[r * cols + c]) {
        if (m_visitedMark[idx] != m_visitedGen) {
          m_visitedMark[idx] = m_visitedGen;
          result.push_back(idx);
        }
      }
    }
  }
  return result;
}

void MeshOverlayItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*)
{
  QRectF exposedRect = option->exposedRect;
  QPainterPath path;
  for (int ei : elementsOverlapping(exposedRect)) {
    const MeshOverlayElement& e = m_mesh->elements[ei];
    if (e.p0 < 0 || e.p0 >= m_mesh->nodes.size() || e.p1 < 0 || e.p1 >= m_mesh->nodes.size() || e.p2 < 0 || e.p2 >= m_mesh->nodes.size())
      continue;
    const MeshOverlayNode& n0 = m_mesh->nodes[e.p0];
    const MeshOverlayNode& n1 = m_mesh->nodes[e.p1];
    const MeshOverlayNode& n2 = m_mesh->nodes[e.p2];

    double triMinX = std::min({ n0.x, n1.x, n2.x });
    double triMaxX = std::max({ n0.x, n1.x, n2.x });
    double triMinY = std::min({ n0.y, n1.y, n2.y });
    double triMaxY = std::max({ n0.y, n1.y, n2.y });
    if (triMaxX < exposedRect.left() || triMinX > exposedRect.right() || triMaxY < exposedRect.top() || triMinY > exposedRect.bottom())
      continue;

    path.moveTo(n0.x, n0.y);
    path.lineTo(n1.x, n1.y);
    path.lineTo(n2.x, n2.y);
    path.lineTo(n0.x, n0.y);
  }

  QPen pen(AppTheme::meshLineColor());
  pen.setCosmetic(true);
  // Width 0, not the QPen(color) ctor's default of 1 -- see
  // GeometryScene.cpp's addNodeItem for why: width-0 cosmetic pens use
  // Qt's fast hairline path, not general stroke tessellation, and are
  // measurably more robust at the extreme accumulated zoom scales a
  // multi-million-element mesh's fine detail can require to inspect.
  pen.setWidth(0);
  painter->setPen(pen);
  painter->drawPath(path);
}
