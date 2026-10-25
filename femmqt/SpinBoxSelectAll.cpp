#include "SpinBoxSelectAll.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QEvent>
#include <QFocusEvent>
#include <QLineEdit>
#include <QPointer>
#include <QTimer>

namespace {

// The spin box `o` is, or whose line edit it is.
QAbstractSpinBox* spinBoxOf(QObject* o)
{
  if (auto* spin = qobject_cast<QAbstractSpinBox*>(o))
    return spin;
  auto* edit = qobject_cast<QLineEdit*>(o);
  return edit ? qobject_cast<QAbstractSpinBox*>(edit->parentWidget()) : nullptr;
}

class SpinBoxSelectAllFilter : public QObject {
  public:
  using QObject::QObject;

  protected:
  bool eventFilter(QObject* watched, QEvent* event) override
  {
    const QEvent::Type t = event->type();
    if (t == QEvent::FocusIn) {
      // Qt focuses the box before the press reaches any filter, so the
      // focusing click is recognised by its FocusIn.
      if (static_cast<QFocusEvent*>(event)->reason() == Qt::MouseFocusReason)
        if (auto* spin = spinBoxOf(watched))
          m_focusClick = spin;
      return false;
    }
    if (t != QEvent::MouseButtonPress && t != QEvent::MouseButtonRelease && t != QEvent::MouseButtonDblClick)
      return false;
    QAbstractSpinBox* spin = qobject_cast<QLineEdit*>(watched) ? spinBoxOf(watched) : nullptr;
    if (t == QEvent::MouseButtonPress) {
      if (spin != m_focusClick) // a press elsewhere, e.g. on the arrows
        m_focusClick = nullptr;
      return false;
    }
    if (!spin || (t == QEvent::MouseButtonRelease && m_focusClick != spin))
      return false;
    m_focusClick = nullptr;
    // After the line edit has handled the event, which would otherwise
    // put the caret or a word selection back.
    QPointer<QAbstractSpinBox> guard(spin);
    QTimer::singleShot(0, spin, [guard]() {
      if (guard)
        guard->selectAll();
    });
    return false;
  }

    private:
  QPointer<QAbstractSpinBox> m_focusClick;
};

} // namespace

void installSpinBoxSelectAll(QApplication* app)
{
  static const QString name = QStringLiteral("femmxSpinBoxSelectAll");
  if (app->findChild<QObject*>(name, Qt::FindDirectChildrenOnly))
    return;
  auto* filter = new SpinBoxSelectAllFilter(app);
  filter->setObjectName(name);
  app->installEventFilter(filter);
}
