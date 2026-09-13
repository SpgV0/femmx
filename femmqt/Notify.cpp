#include "Notify.h"

#include <QWidget>

#include <cstdio>

namespace {

bool g_headless = false;
bool g_problem = false;

// One line, prefixed so a batch log makes it obvious which process
// spoke. Title and text both, because the title is usually the only
// thing that says which operation failed.
void toStderr(const char* level, const QString& title, const QString& text)
{
  QString flat = text;
  flat.replace('\n', QLatin1String(" "));
  fprintf(stderr, "femmqt: %s: %s: %s\n", level, qPrintable(title), qPrintable(flat));
  fflush(stderr);
}

} // namespace

void Notify::setHeadless(bool headless)
{
  g_headless = headless;
}

bool Notify::isHeadless()
{
  return g_headless;
}

void Notify::warning(QWidget* parent, const QString& title, const QString& text)
{
  g_problem = true;
  if (g_headless) {
    toStderr("error", title, text);
    return;
  }
  QMessageBox::warning(parent, title, text);
}

void Notify::information(QWidget* parent, const QString& title, const QString& text)
{
  if (g_headless) {
    // Not a problem -- information is progress, not failure, and
    // flagging it would make hadProblem() useless.
    toStderr("note", title, text);
    return;
  }
  QMessageBox::information(parent, title, text);
}

QMessageBox::StandardButton Notify::question(QWidget* parent, const QString& title,
    const QString& text, QMessageBox::StandardButtons buttons,
    QMessageBox::StandardButton defaultButton,
    QMessageBox::StandardButton headlessAnswer)
{
  if (g_headless) {
    toStderr("note", title, text);
    return headlessAnswer;
  }
  return QMessageBox::question(parent, title, text, buttons, defaultButton);
}

bool Notify::hadProblem()
{
  return g_problem;
}

void Notify::clearProblem()
{
  g_problem = false;
}
