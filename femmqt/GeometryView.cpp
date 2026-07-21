#include "GeometryView.h"

#include "GeometryScene.h"

#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#ifdef FEMMQT_HAVE_OPENGL
#include <QOpenGLWidget>
#endif
#include <QShowEvent>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>

GeometryView::GeometryView(QGraphicsScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent)
{
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  setResizeAnchor(QGraphicsView::AnchorUnderMouse);

#ifdef FEMMQT_HAVE_OPENGL
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
  // per user request to prioritize pan/zoom responsiveness -- moves
  // rasterization onto the GPU, same rationale as SolutionGraphicsView's
  // identical change (see that constructor's comment). Set before
  // m_cursorTooltip below (a QWidget child of viewport()) for the same
  // ordering reason -- setViewport() replaces/destroys the prior viewport
  // widget, which would orphan a child created before this call.
  setViewport(new QOpenGLWidget(this));
#endif
  // Without this, QGraphicsScene::mouseMoveEvent only fires while a
  // button is held (dragging) -- both the status bar's live coordinate
  // readout and this view's own floating cursor tooltip need it on every
  // hover move too.
  setMouseTracking(true);
  // Needed to actually receive the Shift key presses/releases the
  // rubber-band-select feature below depends on -- a QGraphicsView with
  // no explicit policy set can end up not holding keyboard focus once a
  // scene item has taken it (e.g. right after clicking a node).
  setFocusPolicy(Qt::StrongFocus);
  m_cursorTooltip = new QLabel(viewport());
  m_cursorTooltip->setStyleSheet(
      "QLabel { background-color: rgba(20, 20, 20, 200); color: white; "
      "padding: 2px 5px; border-radius: 3px; font-size: 11px; }");
  m_cursorTooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
  m_cursorTooltip->hide();
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

void GeometryView::mouseMoveEvent(QMouseEvent* event)
{
  QGraphicsView::mouseMoveEvent(event);

  QPointF scenePos = mapToScene(event->pos());
  if (auto* gs = qobject_cast<GeometryScene*>(scene()))
    scenePos = gs->snapPoint(scenePos);

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
  // was setViewportUpdateMode(QGraphicsView::FullViewportUpdate) on the
  // whole view (see git history) -- forced a full repaint on every wheel
  // zoom and toolbar/scrollbar pan too, not just this hover-move case,
  // which is exactly the "zoom/pan isn't smooth" complaint this was
  // rewritten to fix. SolutionGraphicsView's mouseMoveEvent (SolutionView.
  // cpp) already solved the identical stale-tooltip-trail problem this
  // way -- ported that fix here instead of reinventing it. Root cause
  // (see that comment for the fuller explanation): MinimalViewportUpdate
  // tracks dirty regions from SCENE changes, not from a plain QWidget
  // child (the tooltip) moving, so a bare viewport()->update(oldRect)
  // is treated as a no-op; scene()->invalidate() is the mechanism
  // MinimalViewportUpdate actually respects for "redraw this region even
  // though nothing scene-side changed."
  QRect oldGeometry = m_cursorTooltip->geometry();

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
  scene()->invalidate(mapToScene(oldGeometry).boundingRect());
}

void GeometryView::leaveEvent(QEvent* event)
{
  m_cursorTooltip->hide();
  QGraphicsView::leaveEvent(event);
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
