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
}

void GeometryView::wheelEvent(QWheelEvent* event)
{
  double factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
  scale(factor, factor);
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
