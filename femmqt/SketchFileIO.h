#pragma once

// Persistence for the constraint/dimension sketch layer (issue #27).
//
// Until this, constraints and dimensions were session-only: close the
// file and every constraint, every dimension and all the parametric
// intent behind the geometry was gone, leaving dumb coordinates. That
// made the CAD layer a drafting aid rather than a design tool -- you
// could not come back tomorrow and change a dimension.
//
// WHY A SIDECAR FILE AND NOT .femx.
//
// .femx was the obvious candidate and is disqualified by its own
// contract. FILE_FORMATS.md defines it as a regenerable CACHE of .fem:
// it records the source's size and mtime, and any mismatch -- or a
// missing, corrupt or wrong-version file -- means "stale", whereupon the
// GUI falls back to the text parser and rewrites the cache. Sketch data
// living there would be silently destroyed the first time anyone touched
// the .fem in the classic editor, or copied only the .fem to another
// machine. A cache is by definition the wrong place for the only copy of
// something.
//
// The .fem file itself is also out: it must stay readable by the classic
// MFC editor and by all four solvers, none of which know anything about
// constraints, and its parsers are explicit per-tag readers that would
// have to be taught to skip whatever we added.
//
// So: "foo.fes" next to "foo.fem". Missing means an unconstrained
// sketch, which is exactly what every existing model is.
//
// ENTITY IDENTITY.
//
// Constraints reference nodes, segments and arcs BY INDEX, and indices
// shift whenever anything is inserted or deleted. An index alone is
// therefore meaningless across sessions. Each reference is stored with a
// geometric fingerprint -- a node's coordinates, a segment's two
// endpoints -- and on load the index is accepted only if the entity
// still sitting there matches. If it does not, the fingerprint is used
// to search for the entity that does; if exactly one matches, the
// reference is repaired, and if none or several do, the constraint is
// dropped and reported rather than silently applied to the wrong
// geometry. "Reported, not silently applied" is the requirement that
// makes this safe to turn on by default.

#include <QString>
#include <QStringList>

struct FemmProblem;

namespace SketchFileIO {

// "foo.fem" -> "foo.fes". Empty in, empty out.
QString sidecarPathFor(const QString& modelPath);

// Writes p's constraints and dimensions. Writing an empty sketch REMOVES
// any existing sidecar rather than leaving a stale one: deleting the last
// constraint has to be persistent too.
//
// Best-effort by design, like the .femx cache: a write failure never
// fails the save the user actually asked for. Returns false and fills
// errorMessage so the caller can say so without aborting.
bool writeSketch(const QString& modelPath, const FemmProblem& p,
    QString& errorMessage);

// Loads into p.constraints / p.dimensions, replacing whatever is there.
// A missing sidecar is SUCCESS with an empty sketch -- every model
// predating this feature is in exactly that state.
//
// `report` receives one human-readable line per constraint or dimension
// that could not be restored, so the caller can show them. A non-empty
// report is not an error: the file loaded, some of the sketch did not.
bool readSketch(const QString& modelPath, FemmProblem& p,
    QStringList& report, QString& errorMessage);

} // namespace SketchFileIO
