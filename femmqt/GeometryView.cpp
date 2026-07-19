#include "GeometryView.h"

#include "GeometryScene.h"

#include <QLabel>
#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>

GeometryView::GeometryView(QGraphicsScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
{
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  setResizeAnchor(QGraphicsView::AnchorUnderMouse);
  // Without this, QGraphicsScene::mouseMoveEvent only fires while a
  // button is held (dragging) -- both the status bar's live coordinate
  // readout and this view's own floating cursor tooltip need it on every
  // hover move too.
  setMouseTracking(true);

  m_cursorTooltip = new QLabel(viewport());
  m_cursorTooltip->setStyleSheet(
      "QLabel { background-color: rgba(20, 20, 20, 200); color: white; "
      "padding: 2px 5px; border-radius: 3px; font-size: 11px; }");
  m_cursorTooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_cursorTooltip->hide();
}

void GeometryView::wheelEvent(QWheelEvent* event)
{
  double factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
  scale(factor, factor);
  event->accept();
}

void GeometryView::mouseMoveEvent(QMouseEvent* event)
{
  QGraphicsView::mouseMoveEvent(event);

  QPointF scenePos = mapToScene(event->pos());
  if (auto* gs = qobject_cast<GeometryScene*>(scene()))
    scenePos = gs->snapPoint(scenePos);

  m_cursorTooltip->setText(QString("%1, %2").arg(scenePos.x(), 0, 'g', 6).arg(scenePos.y(), 0, 'g', 6));
  m_cursorTooltip->adjustSize();

  // Offset down-right of the cursor (matches Fusion 360/most CAD tools'
  // own convention) -- clamped so the tooltip never runs off the right/
  // bottom edge of the viewport when the cursor is near it.
  QPoint pos = event->pos() + QPoint(16, 16);
  pos.setX(std::min(pos.x(), viewport()->width() - m_cursorTooltip->width()));
  pos.setY(std::min(pos.y(), viewport()->height() - m_cursorTooltip->height()));
  m_cursorTooltip->move(pos);
  m_cursorTooltip->show();
  m_cursorTooltip->raise();
}

void GeometryView::leaveEvent(QEvent* event)
{
  m_cursorTooltip->hide();
  QGraphicsView::leaveEvent(event);
}
