#include "GeometryView.h"

#include "GeometryScene.h"

#include <QKeyEvent>
#ifdef FEMMQT_HAVE_OPENGL
#include <QOpenGLWidget>
#endif
#include <QShowEvent>
#include <QTimer>
#include <QWheelEvent>

GeometryView::GeometryView(QGraphicsScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
{
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  setResizeAnchor(QGraphicsView::AnchorUnderMouse);
  // NOTE (Claude, Anthropic, 2026-07-26): a stale drag-trail artifact was
  // investigated at length here. Ruled out, in order: (1)
  // ItemIgnoresTransformations -- switching node/block-label markers to
  // normal scene-space geometry (see GeometryScene::
  // refreshFixedPixelItemSizes()) produced the exact same artifact; (2)
  // the selection drop-shadow effect -- disabling it entirely left the
  // artifact unchanged. Root cause found by pixel-sampling the leftover
  // patches: exactly double the true background's brightness (30,30,30 vs
  // 60,60,60) -- which is precisely AppTheme::gridLine()'s own dark-theme
  // color (GeometryScene::drawBackground draws antialiased grid dots in
  // that color). With antialiasing on (see setRenderHint below) and no
  // background caching, a fast drag queues many overlapping partial
  // repaint requests before Qt gets to actually paint any of them;
  // drawBackground() re-runs its full antialiased dot-drawing loop for
  // EACH one, and repeated partial-coverage compositing of the same
  // antialiased dots onto themselves (instead of a clean erase-then-draw)
  // pushes them visibly brighter each time, most severely wherever many
  // overlapping requests piled up -- exactly the drag path. CacheBackground
  // fixes this at the actual source: it renders drawBackground()'s output
  // into an offscreen pixmap ONCE and blits (not recomposites) it for
  // subsequent partial repaints, so this compositing accumulation can't
  // happen regardless of how many overlapping updates queue up.
  setCacheMode(QGraphicsView::CacheBackground);

#ifdef FEMMQT_HAVE_OPENGL
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // per user request to prioritize pan/zoom responsiveness -- moves
  // rasterization onto the GPU, same rationale as SolutionGraphicsView's
  // identical change (see that constructor's comment).
  setViewport(new QOpenGLWidget(this));
#endif
  // Without this, QGraphicsScene::mouseMoveEvent only fires while a
  // button is held (dragging) -- the status bar's live coordinate
  // readout (MainWindow::m_positionLabel, via GeometryScene::
  // mousePositionChanged) needs it on every hover move too. A cursor-
  // following floating tooltip used to need this too; removed 2026-07-21
  // (see git history) -- it required a per-mouse-move QWidget move() +
  // scene()->invalidate() to avoid leaving a stale trail, and was the
  // root cause of a "zoom/pan isn't smooth" complaint even after being
  // scoped down to just the moves that needed it. The stationary status-
  // bar readout shows the same coordinate with none of that cost.
  setMouseTracking(true);
  // Needed to actually receive the Shift key presses/releases the
  // rubber-band-select feature below depends on -- a QGraphicsView with
  // no explicit policy set can end up not holding keyboard focus once a
  // scene item has taken it (e.g. right after clicking a node).
  setFocusPolicy(Qt::StrongFocus);
}

void GeometryView::fitInViewSafe(const QRectF& rect)
{
  if (rect.isEmpty())
    return;
  QGraphicsView::ViewportAnchor prevAnchor = transformationAnchor();
  setTransformationAnchor(QGraphicsView::AnchorViewCenter);
  fitInView(rect, Qt::KeepAspectRatio);
  setTransformationAnchor(prevAnchor);
  refreshMarkerSizes();
}

void GeometryView::zoomBy(double factor)
{
  scale(factor, factor);
  refreshMarkerSizes();
}

void GeometryView::resetZoomTransform()
{
  resetTransform();
  scale(1, -1);
  refreshMarkerSizes();
}

void GeometryView::refreshMarkerSizes()
{
  if (auto* gs = qobject_cast<GeometryScene*>(scene()))
    gs->refreshFixedPixelItemSizes();
}

void GeometryView::wheelEvent(QWheelEvent* event)
{
  double factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
  zoomBy(factor);
  event->accept();
}

void GeometryView::keyPressEvent(QKeyEvent* event)
{
  // Only in Select mode -- GeometryScene::mousePressEvent already routes
  // every OTHER tool mode's clicks to handleToolClick() regardless of
  // this view's drag mode, but arming RubberBandDrag while e.g. Add Node
  // is active would still be pointless (nothing to multi-select while
  // placing geometry) and needlessly changes drag behavior for a tool
  // that doesn't use it.
  if (event->key() == Qt::Key_Shift && !event->isAutoRepeat()) {
    auto* gs = qobject_cast<GeometryScene*>(scene());
    if (gs && gs->toolMode() == GeometryToolMode::Select)
      setDragMode(QGraphicsView::RubberBandDrag);
  }
  QGraphicsView::keyPressEvent(event);
}

void GeometryView::keyReleaseEvent(QKeyEvent* event)
{
  if (event->key() == Qt::Key_Shift && !event->isAutoRepeat())
    setDragMode(QGraphicsView::NoDrag);
  QGraphicsView::keyReleaseEvent(event);
}

void GeometryView::showEvent(QShowEvent* event)
{
  QGraphicsView::showEvent(event);
  // Deferred to the next event-loop iteration: a plain setFocus() here
  // gets clobbered by MainWindow's own initial focus assignment (toolbar/
  // dock widgets also vying for it during the same show() cascade) --
  // confirmed directly, an immediate setFocus() call left a fresh window
  // still failing a first-interaction Shift+drag. Running after
  // everything else's initial-show focus traffic has settled reliably
  // wins.
  QTimer::singleShot(0, this, [this]() { setFocus(); });
}
