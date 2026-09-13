#pragma once

// ProblemFileIO.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #80).
//
// One reader and one writer for all four FEMM model formats. What they
// share -- the common header scalars, the node/segment/arc/hole/block
// rows and the whole section-dispatch loop -- is written once here. What
// differs goes to PropertyCodec (the property sections and the per-kind
// header scalars) and to the small kind-aware parts of the geometry rows
// noted below.
//
// THE GEOMETRY IS NOT QUITE IDENTICAL, which is worth stating plainly
// because #79 assumed it was. Two differences, both of them about where
// a physics attaches its SOURCE:
//
//   * .fee/.feh/.fec put a conductor index on every node, segment and
//     arc. .fem has no such column: magnetics attaches a circuit to
//     block labels instead, because a circuit carries current through a
//     region while a conductor is an equipotential surface.
//
//   * the block-label row is nine columns for magnetics (circuit, MagDir
//     and turns among them) and six for the other three.
//
// Everything else really is the same, including the holes section.

#include <QString>

struct FemmProblem;
enum class FemmProblemKind;

namespace ProblemFileIO {

// Reads any of the four, choosing the kind from the file's extension and
// setting problem.kind accordingly. An unrecognised extension is an
// error rather than a guess -- reading a .fee as magnetics would parse
// its <Vp> tags as nothing and hand back a model that is silently empty
// of properties.
bool read(const QString& path, FemmProblem& problem, QString& errorMessage);

// Writes in the format matching problem.kind.
//
// REFUSES a path whose extension belongs to a different kind, rather
// than writing one format under another's name. That file would open
// again as whatever its extension says and lose every property in it.
bool write(const QString& path, const FemmProblem& problem, QString& errorMessage);

// Reads with the kind forced, for callers that already know it (the
// .femx cache path, and FemmFileIO's magnetics-only entry points).
bool readAs(const QString& path, FemmProblemKind kind, FemmProblem& problem,
    QString& errorMessage);

} // namespace ProblemFileIO
