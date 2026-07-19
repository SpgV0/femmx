#include "GeometryView.h"

#include <QWheelEvent>

GeometryView::GeometryView(QGraphicsScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
{
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  setResizeAnchor(QGraphicsView::AnchorUnderMouse);
  // Without this, QGraphicsScene::mouseMoveEvent only fires while a
  // button is held (dragging) -- the status bar's live coordinate
  // readout needs it on every hover move too.
  setMouseTracking(true);
}

void GeometryView::wheelEvent(QWheelEvent* event)
{
  double factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
  scale(factor, factor);
  event->accept();
}
