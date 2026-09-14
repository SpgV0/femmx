#pragma once

// WindowTitle.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-14.
//
// What the title bar says, as a function rather than as a string built
// in a slot.
//
// Per direct user request: the problem type belongs in the title. Since
// #80 a document is one of four physics, chosen at File > New and
// unchangeable afterwards, and until now nothing on screen said which
// one you were in -- the toolbar is identical for all four, and the
// difference only surfaces once you open a property dialog. A .fee and
// a .feh look the same until you ask.
//
// Pulled out of MainWindow::updateTitle so it can be tested without
// standing up a QMainWindow, and so the editor and the Solution Viewer
// cannot drift into describing the same document two different ways.

#include "FemmProblem.h"

#include <QString>

namespace WindowTitle {

// "FEMMX (Qt) - Magnetics - C:/models/coil.fem*"
//
// `path` empty means an untitled document. `demoTitle`, when non-empty,
// replaces the path entirely: a demo's working copy lives in a
// temporary directory, and showing that path would be noise where the
// demo's own name is the useful thing (#91).
QString forEditor(FemmProblemKind kind, const QString& path, bool dirty,
    const QString& demoTitle = QString());

// "FEMMX (Qt) - Heat Flow Solution - example.anh"
//
// The viewer used to say "FEMMX (Qt) - Solution Viewer - <path>" for
// magnetics and "<Kind> Solution -- <file>" for the other three, which
// is two answers to the same question depending on which file you
// happened to open.
QString forSolution(FemmProblemKind kind, const QString& path);

} // namespace WindowTitle
