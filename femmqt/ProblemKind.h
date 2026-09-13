#pragma once

// ProblemKind.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #80).
//
// Everything that differs between the four problem kinds and is NOT a
// file-format field: the extension, the solver binary, what each list of
// properties is called in that physics, and a type-blind way to read and
// rename those lists.
//
// The point of the access path is that ordinary UI code -- a property
// list box, a delete button, a combo of material names -- needs only the
// NAME and the COUNT, and those are the same question in all four
// physics. Only the property dialogs genuinely need per-kind fields, and
// those are per-kind by nature (see #81). Without this, every list and
// every combo in the app grows a four-way switch.

#include <QString>
#include <QStringList>

struct FemmProblem;
enum class FemmProblemKind;

namespace ProblemKind {

// The four lists every one of the formats has exactly one of.
enum class Category {
  Point,    // [PointProps]
  Boundary, // [BdryProps]
  Material, // [BlockProps]
  Source,   // [CircuitProps] for magnetics, [ConductorProps] otherwise
};

// "Magnetics", "Electrostatics", "Heat Flow", "Current Flow".
QString displayName(FemmProblemKind kind);

// "fem" / "fee" / "feh" / "fec", without the dot.
QString extension(FemmProblemKind kind);

// The kind a path's extension implies. Returns false for anything else,
// which is how Open tells a model from a solution or a stray file.
bool kindForPath(const QString& path, FemmProblemKind& kindOut);

// "fkn.exe" / "belasolv.exe" / "hsolv.exe" / "csolv.exe" -- the four
// binaries that already ship. No new solvers are involved anywhere in
// this work.
QString solverExecutable(FemmProblemKind kind);

// The solution-file extension this kind's solver produces, for the
// post-processor: "ans" / "res" / "anh" / "anc".
QString solutionExtension(FemmProblemKind kind);

// What this physics calls a category: a magnetics [BlockProps] entry is
// a "Material", an electrostatics one is also a "Material", but the
// source list is "Circuits" in magnetics and "Conductors" in the other
// three. Used for menu items, dialog titles and list headers.
QString categoryLabel(FemmProblemKind kind, Category category);

// --- the type-blind access path -------------------------------------------

int count(const FemmProblem& p, Category category);
QString name(const FemmProblem& p, Category category, int index);
QStringList names(const FemmProblem& p, Category category);

// Renames in place. Ignores an out-of-range index rather than asserting:
// callers are UI code reacting to a selection that may have gone stale.
void setName(FemmProblem& p, Category category, int index, const QString& newName);

} // namespace ProblemKind
