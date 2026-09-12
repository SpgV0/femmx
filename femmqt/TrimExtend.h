#pragma once

// TrimExtend.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12
// (issue #29).
//
// Trim, extend and split: the three operations that turn "draw it the
// right length first time" into "draw it long and cut it back", which is
// how sketching actually works. femmqt could add geometry and delete
// geometry but not MODIFY it, and the workaround -- delete and redraw --
// is where stray fragments and near-miss junctions come from.
//
// All three live here rather than in FemmProblemEdit because they share
// one non-trivial primitive that nothing else needs: the set of
// parameters along an entity at which other geometry cuts it. Trim
// removes the piece between two of those, split makes one where the user
// clicked, and extend is the same search run past the end.
//
// WHAT COUNTS AS A CUT. Deliberately broader than
// FemmProblemEdit::splitIntersectingSegments, which only handles proper
// crossings because its job is to make crossing lines share a vertex.
// Here an entity is also cut where another entity's ENDPOINT lands on it
// (a T-junction -- the single most common thing endpoint snapping
// produces) and where a bare node happens to sit on it. Ignoring those
// would make trim refuse to cut at exactly the junctions a user just
// drew.
//
// NODES ARE REUSED, NEVER DUPLICATED. Where a cut lands on an existing
// node, that node is used. Inventing a second node on top of it would
// produce geometry that looks joined and does not mesh as a bounded
// region -- the same failure class the snapping work (#28) exists to
// prevent.

#include <QString>
#include <QVector>

struct FemmProblem;

namespace TrimExtend {

enum class EntityKind {
  Segment,
  Arc,
};

// What an operation did, or why it did nothing.
//
// `ok == false` always carries a `message` naming the reason in terms of
// the drawing, not the code -- these are user-triggered operations whose
// failures are almost always geometric ("nothing bounds this line") and
// not errors.
struct Result {
  bool ok = false;
  QString message;

  // Split, and the trim cases that cut rather than delete: the nodes
  // introduced (or reused) at the cut points. -1 where unused.
  int nodeA = -1;
  int nodeB = -1;

  // How many constraints/dimensions referenced the entity that was
  // modified. They are NOT dropped -- the entity keeps its index, so
  // every reference stays valid -- but a dimension that measured the
  // whole line now measures the piece that survived, which the user
  // needs telling about. Zero for the common case.
  int affectedReferences = 0;
};

// The parameters, strictly inside (0,1), at which other geometry cuts
// entity `index`. Sorted ascending and de-duplicated. Exposed for tests
// and for a future on-canvas trim preview.
//
// Parameterisation: a segment runs t=0 at n0 to t=1 at n1; an arc runs
// t=0 at n0 sweeping counterclockwise through arcLength degrees to t=1
// at n1, matching the convention the rest of the editor uses (see
// FemmProblemEdit::circleFromArc).
QVector<double> cutParameters(const FemmProblem& p, EntityKind kind, int index);

// Insert a node at the point of the entity nearest (x,y) and split it in
// two. Both halves inherit every property of the original (boundary
// marker, group, mesh size, hidden). The original index keeps the piece
// containing t=0.
Result split(FemmProblem& p, EntityKind kind, int index, double x, double y);

// Remove the portion of the entity containing (x,y), bounded by the
// nearest cut on either side.
//
// With no cut at all the whole entity goes. That is SolidWorks'/Fusion's
// sketch-trim behaviour rather than AutoCAD's refuse-without-a-cutting-
// edge, and it is the right one here: in a sketch the overwhelmingly
// common reason to click an unbounded stray with the trim tool is to be
// rid of it.
Result trim(FemmProblem& p, EntityKind kind, int index, double x, double y);

// Extend the end of the entity nearest (x,y) until it meets the first
// other entity in its path, moving that end node there.
//
// Refused when the end node is shared with other geometry: moving it
// would drag that geometry along, and re-pointing this entity at a new
// node would tear the junction open. Both are worse than being told no.
Result extend(FemmProblem& p, EntityKind kind, int index, double x, double y);

} // namespace TrimExtend
