#pragma once

// OffsetChamfer.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12
// (issue #30).
//
// Offset and chamfer -- the other two corner/parallel constructions.
// "Create Radius" (a fillet) was the whole of that toolset, and offset
// in particular is how the geometry FEMM models are made of actually
// gets drawn: an air gap, a coating, a lamination, a conductor's
// insulation. Without it, drawing a parallel curve means working out its
// coordinates by hand and typing them.
//
// CHAMFER is the flat counterpart to Create Radius and deliberately
// mirrors it: same corner selection (one node shared by exactly two
// entities), same property inheritance, same
// rewire-then-delete-the-corner sequence. It requires two SEGMENTS,
// though -- see chamfer() for why an arc corner is refused rather than
// approximated.
//
// OFFSET ADDS geometry and leaves the original alone, the way every CAD
// package's offset does. That is what makes it useful for a gap or a
// coating: you want both curves.
//
// THE BOUNDARY MARKER IS NOT CARRIED ONTO AN OFFSET, and that is a
// deliberate difference from Create Radius. A fillet REPLACES the corner
// it cuts, so inheriting the boundary condition keeps the model saying
// what it said before. An offset is a second curve alongside one that
// still has the condition on it -- copying it would quietly impose the
// same boundary condition in two places, which changes the physics
// rather than the drawing. Group and mesh size do come across, since
// those are about meshing rather than about what is being solved. The
// result says so, so it is a stated choice rather than a surprise.

#include <QString>
#include <QVector>

struct FemmProblem;

namespace OffsetChamfer {

// How the join between two consecutive offset pieces is closed where
// the turn goes AWAY from the offset side -- the outside of a corner,
// where the two offsets no longer reach each other.
//
// The choice is the user's because the two are genuinely different
// shapes, not one being better: a miter keeps the corner sharp, which is
// what you want offsetting a laminated core or a slot, and grows without
// limit as the corner gets sharper (offset a 5-degree corner and the
// spike is over 20 times the offset distance). A fillet keeps the new
// curve at a constant distance from the old one everywhere, which is
// what "a 0.5 mm coating" actually means, at the cost of rounding a
// corner that was square.
//
// The INSIDE of a corner is always extend-and-intersect regardless: the
// two offsets overlap there, and rounding it would leave the curve
// closer to the original than the distance asked for.
enum class CornerStyle {
  Miter,  // extend both to their intersection
  Fillet, // an arc of radius |distance| about the original corner
};

struct Result {
  bool ok = false;
  QString message;

  // Offset: how many segments and arcs were created, and how many
  // corners needed a fillet instead of an extend-and-intersect.
  int segmentsCreated = 0;
  int arcsCreated = 0;
  int cornersFilleted = 0;
};

// Offset every selected segment and arc by `distance`, to the LEFT of
// each chain's direction of travel for a positive value and to the right
// for a negative one. Chains are assembled from the selection by shared
// nodes, so a connected run offsets as one curve with its corners
// resolved, and an isolated entity offsets on its own.
//
// Corners are closed according to `corners` on the outside of a turn,
// and always by extend-and-intersect on the inside. See CornerStyle.
Result offsetSelection(FemmProblem& p, double distance, CornerStyle corners);

// Cut the corner at node `n` with a straight segment, `distance0` back
// along one edge and `distance1` back along the other.
Result chamferDistances(FemmProblem& p, int n, double distance0, double distance1);

// Cut the corner at node `n` `distance0` back along the first edge, with
// the cut making `angleDeg` to that edge.
Result chamferDistanceAngle(FemmProblem& p, int n, double distance0, double angleDeg);

// Whether node `n` is a corner these can work on: shared by exactly two
// SEGMENTS. Mirrors FemmProblemEdit::canCreateRadius, but narrower --
// see chamfer()'s own comment.
bool canChamfer(const FemmProblem& p, int n);

} // namespace OffsetChamfer
