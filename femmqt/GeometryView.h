#pragma once

#include <QGraphicsView>

class QLabel;

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
  void mouseMoveEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;

  private:
  // Floats next to the cursor showing the model-space coordinate under
  // it, matching how Fusion 360/most modern CAD tools show a live
  // coordinate readout that tracks the cursor itself rather than only a
  // fixed status-bar corner (which MainWindow's own position label still
  // also provides -- this is additive, not a replacement).
  QLabel* m_cursorTooltip = nullptr;
};
