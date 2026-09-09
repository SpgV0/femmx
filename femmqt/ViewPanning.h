#pragma once

// Click-drag panning, shared by the geometry editor's GeometryView and
// the Solution Viewer's SolutionGraphicsView.
//
// Added per direct user request, following the fix that let the Solution
// Viewer scroll past the edge of its model at all: navigation was
// toolbar buttons, arrow keys and scrollbars only, with no way to just
// grab the canvas and drag it the way the classic GUI and every modern
// CAD tool allow.
//
// MIDDLE or RIGHT button drags the view. Middle is the CAD convention
// (Fusion 360, SolidWorks, FreeCAD, KiCad all use it); right is carried
// as a fallback for mice and trackpads with no middle button, and is
// free to take here because neither view has ever had a context menu --
// checked before claiming it, there is no contextMenuEvent or
// Qt::RightButton handling anywhere in femmqt.
//
// Left is deliberately untouched: it belongs to the drawing tools,
// selection, Shift+rubber-band in the editor, and the zoom-window
// rubber band in the viewer. That is also why QGraphicsView's built-in
// ScrollHandDrag mode is not used -- it hijacks the left button
// wholesale, which would break every one of those.
//
// This is a header-only helper rather than duplicated code in both
// views because the two would otherwise drift; the codebase's existing
// per-view duplication (fitInViewSafe) already needs cross-referencing
// comments to stay in step.

#include <QGraphicsView>
#include <QMouseEvent>
#include <QPoint>
#include <QScrollBar>

class DragPanState {
  public:
  static bool isPanButton(Qt::MouseButton b)
  {
    return b == Qt::MiddleButton || b == Qt::RightButton;
  }

  bool active() const { return m_active; }

  // Each returns true if it consumed the event, in which case the
  // caller must return immediately without running its own handling.

  bool begin(QGraphicsView* view, QMouseEvent* event)
  {
    if (m_active || !isPanButton(event->button()))
      return false;
    m_active = true;
    m_lastPos = event->pos();
    // Save and restore rather than unsetCursor(): the Solution Viewer's
    // zoom-window mode sets its own crosshair on the viewport, and a
    // pan that ended with a blind unset would silently clear it.
    m_savedCursor = view->viewport()->cursor();
    view->viewport()->setCursor(Qt::ClosedHandCursor);
    event->accept();
    return true;
  }

  bool update(QGraphicsView* view, QMouseEvent* event)
  {
    if (!m_active)
      return false;
    const QPoint delta = event->pos() - m_lastPos;
    m_lastPos = event->pos();
    // Scrollbars are in viewport space, so this is independent of the
    // scale(1,-1) both views apply for y-up problem coordinates.
    // Subtracting makes the content follow the cursor, which is the
    // grab-and-drag direction users expect.
    QScrollBar* h = view->horizontalScrollBar();
    QScrollBar* v = view->verticalScrollBar();
    h->setValue(h->value() - delta.x());
    v->setValue(v->value() - delta.y());
    event->accept();
    return true;
  }

  bool end(QGraphicsView* view, QMouseEvent* event)
  {
    if (!m_active || !isPanButton(event->button()))
      return false;
    m_active = false;
    view->viewport()->setCursor(m_savedCursor);
    event->accept();
    return true;
  }

  private:
  bool m_active = false;
  QPoint m_lastPos;
  QCursor m_savedCursor;
};
