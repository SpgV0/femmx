#pragma once

struct FemmProblem;

// Mutating operations on a FemmProblem that keep node-index references
// (FemmSegment::n0/n1, FemmArcSegment::n0/n1) consistent -- kept separate
// from FemmProblem.h (plain data) and GeometryScene (rendering/input), so
// the index-renumbering logic has one authoritative place. Nothing here
// touches Qt graphics types.
namespace FemmProblemEdit {

int addNode(FemmProblem& p, double x, double y);
int addSegment(FemmProblem& p, int n0, int n1);
int addBlockLabel(FemmProblem& p, double x, double y);

// Deleting a node also deletes every segment/arc that references it (a
// segment can't exist with a dangling endpoint), then shifts every
// remaining segment/arc's node indices down to account for the removed
// slot -- this renumbering is the actual reason this isn't just a plain
// QVector::remove call at the GeometryScene layer.
void deleteNode(FemmProblem& p, int nodeIndex);
void deleteSegment(FemmProblem& p, int segmentIndex);
void deleteArcSegment(FemmProblem& p, int arcIndex);
void deleteBlockLabel(FemmProblem& p, int blockLabelIndex);

} // namespace FemmProblemEdit
