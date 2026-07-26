#include "HoverTooltip.h"

#include <QAction>
#include <QCursor>
#include <QEvent>
#include <QPointer>
#include <QTimer>
#include <QToolBar>
#include <QToolTip>
#include <QWidget>

namespace {

constexpr int kDelayMs = 2000;

// One filter instance per toolbar (parented to it, so it's destroyed
// along with it) -- only one button can be hovered at a time, so a
// single shared timer per toolbar is enough; no need for one per button.
class Filter : public QObject {
  public:
  explicit Filter(QObject* parent)
      : QObject(parent)
  {
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(kDelayMs);
    connect(m_timer, &QTimer::timeout, this, &Filter::showNow);
  }

  protected:
  bool eventFilter(QObject* watched, QEvent* event) override
  {
    switch (event->type()) {
    case QEvent::Enter:
      m_pending = qobject_cast<QWidget*>(watched);
      m_timer->start();
      break;
    case QEvent::Leave:
    case QEvent::MouseButtonPress:
      m_timer->stop();
      m_pending = nullptr;
      QToolTip::hideText();
      break;
    case QEvent::ToolTip:
      // Suppress Qt's own near-instant automatic tooltip -- this filter's
      // Enter-triggered timer above shows the same text later instead.
      return true;
    default:
      break;
    }
    return QObject::eventFilter(watched, event);
  }

  private:
  void showNow()
  {
    if (!m_pending)
      return;
    QString text = m_pending->toolTip();
    if (!text.isEmpty())
      QToolTip::showText(QCursor::pos(), text, m_pending);
  }

  QTimer* m_timer = nullptr;
  QPointer<QWidget> m_pending;
};

} // namespace

void HoverTooltip::installOn(QToolBar* bar)
{
  auto* filter = new Filter(bar);
  for (QAction* action : bar->actions()) {
    QWidget* w = bar->widgetForAction(action);
    if (w)
      w->installEventFilter(filter);
  }
}
