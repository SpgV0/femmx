#pragma once

// ConstructionGeometry.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12
// (issue #31).
//
// The three construction primitives that are worth having as commands
// rather than as something to assemble by hand every time, plus the
// toggle that moves geometry between construction and real.
//
// Everything here only ever SETS the flag; what the flag means -- not
// written to the .fem, not written to the .femx, not meshed, drawn
// dashed and dimmed, and persisted in the .fes sidecar -- lives with the
// code that honours it. See FemmSegment::isConstruction.

#include <QString>

struct FemmProblem;

namespace ConstructionGeometry {

struct Result {
  bool ok = false;
  QString message;
  int entitiesChanged = 0;
};

// Flip the selected nodes, segments and arcs between construction and
// real.
//
// `toConstruction` decides the direction rather than inverting each
// entity individually: inverting a mixed selection is never what anyone
// means, and leaves you unable to say which half you got.
Result setSelectedConstruction(FemmProblem& p, bool toConstruction);

// A construction line through (x0,y0)-(x1,y1). The everyday case is a
// symmetry axis or a centreline, which is why it exists as a command:
// in an axisymmetric model the r=0 axis is referenced constantly and is
// not part of the geometry.
Result addCentreline(FemmProblem& p, double x0, double y0, double x1, double y1);

// `count` construction nodes evenly spaced around a circle of radius
// `radius` about (cx,cy), starting at `startAngleDeg`, plus the circle
// itself as two construction arcs.
//
// The nodes are the point of it: they are what pole pieces, slots and
// bolt holes get constrained to, and placing them by hand means typing
// count*2 coordinates that each have a cosine in them.
Result addBoltCircle(FemmProblem& p, double cx, double cy, double radius,
    int count, double startAngleDeg);

// A closed construction rectangle between two opposite corners -- a
// reference frame to constrain real geometry against.
Result addReferenceRectangle(FemmProblem& p, double x0, double y0, double x1, double y1);

} // namespace ConstructionGeometry
