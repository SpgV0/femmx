#pragma once

#include <QGraphicsView>

// Thin QGraphicsView subclass adding mouse-wheel zoom -- a standard
// modern-CAD gesture the classic GUI doesn't have (it only offers
// menu/keyboard zoom), added as a value-add consistent with the "modern
// CAD" motivation for this GUI rather than a strict parity port.
class GeometryView : public QGraphicsView {
  Q_OBJECT

  public:
  explicit GeometryView(QGraphicsScene* scene, QWidget* parent = nullptr);

  protected:
  void wheelEvent(QWheelEvent* event) override;
};
