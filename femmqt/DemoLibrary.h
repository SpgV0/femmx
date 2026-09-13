#pragma once

// DemoLibrary.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #91, part of #89).
//
// The shipped demo models, and the rule that makes them safe to ship.
//
// "Can be opened and solved, but never modified in its installed
// location" sounds like a file-permission problem. It is not. femmqt
// writes THREE different things next to whatever model it has open:
//
//   .femx   the binary cache, written on open and on save
//   .fes    the sketch sidecar (#27)
//   the solve pipeline -- SolveRunner sets its working directory to the
//           model's own folder, so triangle.exe drops .poly/.node/.ele
//           there and the solver writes the .ans/.anh/.res/.anc beside
//           the model
//
// This is not hypothetical: rendering the seven magnetics demos once,
// while building the corpus, left seven .femx files in demos/. They are
// gitignored, so nothing complained -- but in an install those are the
// shipped files, modified by nothing more than looking at them.
//
// Nor can filesystem permissions be leaned on: FEMM installs to
// C:\femm42, which a normal user can write.
//
// So the file that is open is never the original. Opening a demo takes
// a working COPY into a writable directory and opens that. Every one of
// the three paths above then writes beside the copy, including the ones
// nobody remembered to guard -- which is the point of choosing this
// remedy over three separate guards.

#include <QString>
#include <QVector>

namespace DemoLibrary {

struct Demo {
  QString file;        // relative to the demos directory, e.g. "magnetics/coax_cable.fem"
  QString title;
  QString problemType; // "magnetics", "electrostatics", "heat flow", "current flow"
  QString description;
  QString analyticReference;

  QString absolutePath() const;
};

// The demos directory, found by walking up from the executable: a dev
// tree has it at <repo>/demos and an install at $INSTDIR\demos, and
// neither needs configuring. Empty when there is none, which is not an
// error -- a build without the corpus simply has no demos.
QString directory();

// Reads demos.json. Returns an empty list and sets `error` when the
// manifest is missing or unreadable; callers are expected to disable
// the menu item rather than report it, since a missing demo library is
// not something the user did (#90).
QVector<Demo> load(QString& error);

// Takes a writable working copy of `demoPath` and returns its path.
//
// Each call gets its own directory, so opening two demos with the same
// base name -- or the same demo twice -- cannot have one solve's output
// land on the other's. The sketch sidecar travels with the model if it
// has one; nothing else does, because everything else is regenerable.
QString makeWorkingCopy(const QString& demoPath, QString& error);

// Where working copies go. Exposed so a test can assert that a copy is
// not inside the demos directory, which is the whole guarantee.
QString workingCopyRoot();

} // namespace DemoLibrary
