#pragma once

// Notify.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #85).
//
// Telling the user something went wrong, without assuming there is a
// user.
//
// femmqt is one executable wearing two hats. Started normally it is a
// desktop application and a modal QMessageBox is exactly right: it
// stops, it says what happened, someone clicks OK. Started with
// --render-png, --convert-ansx, --import-dxf, --probe-ans or
// --test-constraints it is a batch tool, usually with no desktop at
// all, and the same QMessageBox is a HANG: the event loop sits on a
// dialog nobody can dismiss, forever. Measured on this repo -- feeding
// --render-png a file extension femmqt did not recognise blocked until
// the process was killed.
//
// A hang is the worst shape this failure could take. It is not a
// crash, so nothing reports it; it is not slow, so a timeout is the
// only thing that distinguishes it from a large model; and under CI it
// consumes the whole job's budget rather than one test's.
//
// So the decision "is there anybody there" is made ONCE, by main(),
// before any window exists, and every user-facing message goes through
// here. Interactive: the dialog, unchanged. Headless: one line on
// stderr, and the process keeps moving so the caller can return a
// non-zero exit code.
//
// The signatures deliberately mirror QMessageBox's statics so a call
// site converts by changing the namespace and nothing else -- the
// wrong kind of "fix" here is one that makes the GUI worse in order to
// serve the CLI.

#include <QMessageBox>
#include <QString>

class QWidget;

namespace Notify {

// Set by main() for every command-line path, before a window is built.
// Not inferred from the QPA platform: offscreen is also how the GUI
// runs under test, and a test that opens a real window still wants the
// real dialog behaviour.
void setHeadless(bool headless);
bool isHeadless();

void warning(QWidget* parent, const QString& title, const QString& text);
void information(QWidget* parent, const QString& title, const QString& text);

// For the one case where the answer changes what happens next. With
// nobody to ask, `headlessAnswer` is what the caller gets -- so pick
// the safe one (Cancel, not Discard).
QMessageBox::StandardButton question(QWidget* parent, const QString& title,
    const QString& text, QMessageBox::StandardButtons buttons,
    QMessageBox::StandardButton defaultButton,
    QMessageBox::StandardButton headlessAnswer);

// Whether any warning() has fired since the last clearProblem(). Lets a
// CLI path turn "something down there reported a failure" into a
// non-zero exit code without every function in between having to grow
// a bool return.
bool hadProblem();
void clearProblem();

} // namespace Notify
