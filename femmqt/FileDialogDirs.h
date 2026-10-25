#pragma once

// FileDialogDirs.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-10-25
// (issue #110).
//
// Where a file dialog starts. Every Open, Save As and DXF dialog used to
// pass an empty directory, so Qt used the process's working directory --
// which, launched from the installed shortcut, was the Start Menu's
// "Programs\FEMMX" folder. Nobody keeps models there, and nothing
// remembered where they did.
//
// The rule, in order:
//   1. the folder of the document being worked on, if it has one --
//      callers pass none for a demo's working copy, which lives in a
//      temp folder where the user's work must not end up;
//   2. the folder this KIND of dialog last chose -- models, DXF,
//      solutions, BH curves and exports are remembered separately,
//      because they usually live in different places;
//   3. the user's Documents folder.
// A remembered folder that has since been deleted is skipped.

#include <QString>

class QSettings;

namespace FileDialogDirs {

// The directory to open a dialog of `kind` in. `currentFile` is the
// open document, or empty.
QString startDir(const QString& kind, const QString& currentFile = QString(),
    QSettings* settings = nullptr);

// startDir() joined with a suggested file name, for Save As.
QString startPath(const QString& kind, const QString& fileName, const QString& currentFile = QString(),
    QSettings* settings = nullptr);

// Records the folder of the path the user chose. Empty paths (a
// cancelled dialog) are ignored.
void remember(const QString& kind, const QString& chosenPath, QSettings* settings = nullptr);

} // namespace FileDialogDirs
