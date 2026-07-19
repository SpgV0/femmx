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
int addArcSegment(FemmProblem& p, int n0, int n1, double arcLengthDeg, double maxSideLengthDeg);
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

// Property-list deletions. Every geometry reference into these lists is
// 1-based (0, or -1 for blockTypeIndex, means "none/unassigned") -- so
// deleting list entry `index` (0-based) means: every reference equal to
// index+1 reverts to "none" (or, for materials, to a hole -- see
// deleteMaterialProp's comment), and every reference greater than index+1
// shifts down by one to stay valid after the entry is removed. The
// count*References functions are for a confirmation-prompt UI
// (PropertyListDialog) and do not themselves mutate anything.
int countPointPropReferences(const FemmProblem& p, int index);
void deletePointProp(FemmProblem& p, int index);

int countBoundaryPropReferences(const FemmProblem& p, int index);
void deleteBoundaryProp(FemmProblem& p, int index);

// A block label referencing the deleted material reverts to blockTypeIndex
// -1 -- the same "hole / not yet assigned" default addBlockLabel() itself
// uses for a brand new label (see its comment) -- rather than being
// deleted outright, since an unassigned label is still valid geometry the
// user can reassign a material to.
int countMaterialPropReferences(const FemmProblem& p, int index);
void deleteMaterialProp(FemmProblem& p, int index);

int countCircuitPropReferences(const FemmProblem& p, int index);
void deleteCircuitProp(FemmProblem& p, int index);

} // namespace FemmProblemEdit
