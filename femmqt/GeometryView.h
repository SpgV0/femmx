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
  // Fusion 360-style multi-select: holding Shift while left-dragging on
  // empty canvas draws a rubber-band rectangle and selects everything it
  // encloses. Only armed in Select tool mode -- see the .cpp for why.
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  // Without this, a freshly-opened window doesn't hand this view Qt
  // keyboard focus until the user clicks inside it once -- StrongFocus
  // alone (set in the constructor) only means this widget CAN take focus,
  // not that it starts with it, so a Shift+drag as literally the first
  // interaction after opening a file would silently fail to arm
  // RubberBandDrag above. Confirmed directly via a real automated
  // session: identical Shift-down/mouse-drag sequence armed
  // RubberBandDrag correctly once *any* prior click had focused this
  // view, and never did on a truly fresh window.
  void showEvent(QShowEvent* event) override;

  private:
  // Floats next to the cursor showing the model-space coordinate under
  // it, matching how Fusion 360/most modern CAD tools show a live
  // coordinate readout that tracks the cursor itself rather than only a
  // fixed status-bar corner (which MainWindow's own position label still
  // also provides -- this is additive, not a replacement).
  QLabel* m_cursorTooltip = nullptr;
};
