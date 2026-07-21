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

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21: per
  // user report ("Zoom to fit... does not work"/"nothing happens" on
  // load) -- root-caused via debug logging against a real 88k-node/
  // 88k-arc/22k-label model (mag_trafo_center_detailed.FEM): the
  // computed itemsBoundingRect() and viewport size were both completely
  // sane (e.g. a 45x22-unit bounds against a 972x674 viewport), yet the
  // view ended up showing almost nothing -- one item in a corner, off by
  // roughly the full width/height of the drawing. The actual cause is
  // this view's transformationAnchor(), set to AnchorUnderMouse (needed
  // for wheel-zoom -- see the constructor). Plain fitInView() still
  // honors that anchor: it re-centers on wherever QCursor::pos() maps to
  // in scene coordinates, which, right after opening a file, is
  // wherever the mouse happened to be left (e.g. still over the File >
  // Open dialog's OK button) -- a point that can be arbitrarily far
  // outside the freshly-loaded drawing's actual extent, dragging the fit
  // off to one side. Confirmed directly: the same fitInView() call
  // looked correct in every test where the mouse had already been
  // deliberately positioned over the canvas first, and only broke when
  // it was called with the cursor left somewhere unrelated -- exactly
  // the real-world "just opened a file" case. Use this instead of
  // calling fitInView() directly anywhere the mouse position isn't
  // already known to be irrelevant: temporarily swaps to
  // AnchorViewCenter for the duration of the call, then restores
  // AnchorUnderMouse so interactive wheel-zoom keeps anchoring under the
  // cursor as intended.
  void fitInViewSafe(const QRectF& rect);

  protected:
  void wheelEvent(QWheelEvent* event) override;
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
};
