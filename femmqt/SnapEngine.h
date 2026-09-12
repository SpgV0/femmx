#pragma once

// Object snapping: snap the cursor to GEOMETRY, not just to the grid
// (issue #28).
//
// femmqt snapped to the grid only, inherited from the classic editor's
// SnapGrid. That is not merely an ergonomic gap. To start a segment
// exactly at an existing node you either land on the grid or you place
// it approximately and repair it afterwards with a Coincident
// constraint -- and geometry that only LOOKS joined does not mesh as a
// bounded region. It is the same class of problem the crossing-segment
// node insertion fixed in v2.2.0, arrived at from the other direction.
//
// Deliberately free of Qt graphics types and of any notion of a view, so
// it can be tested directly. Zoom-independence lives at the CALL SITE:
// the caller converts a pixel capture radius into model units using the
// view's current scale and passes that in. Putting the conversion here
// would mean this file needed to know about views.

#include <QVector>

struct FemmProblem;

namespace SnapEngine {

// Ordered by priority, highest first, and the order is the whole design:
// when several targets are within the capture radius the most SPECIFIC
// one wins. An endpoint beats a midpoint beats a point somewhere along
// the edge, because a user hovering near a corner means the corner.
enum class SnapType {
  Endpoint,      // an existing node
  Intersection,  // where two edges cross, whether or not a node is there
  Centre,        // an arc's centre of curvature
  Midpoint,      // the middle of a segment
  Quadrant,      // an arc's 0/90/180/270-degree points
  Perpendicular, // the foot of the perpendicular from a reference point
  Tangent,       // touch point of a tangent from a reference point
  OnEdge,        // nearest point on a segment or arc
  Grid,          // the existing behaviour, lowest priority
  None,          // nothing in range; the cursor is used unchanged
};

// One bit per type, so Preferences can enable them individually and the
// caller can pass a single value around.
enum SnapFlags : unsigned {
  SnapNone = 0u,
  SnapEndpoint = 1u << 0,
  SnapIntersection = 1u << 1,
  SnapCentre = 1u << 2,
  SnapMidpoint = 1u << 3,
  SnapQuadrant = 1u << 4,
  SnapPerpendicular = 1u << 5,
  SnapTangent = 1u << 6,
  SnapOnEdge = 1u << 7,
  SnapGrid = 1u << 8,
  // Everything except the two that need a reference point, which are
  // only meaningful while drawing from somewhere.
  SnapDefault = SnapEndpoint | SnapIntersection | SnapCentre | SnapMidpoint
      | SnapQuadrant | SnapOnEdge | SnapGrid,
  SnapAll = 0x1ffu,
};

struct SnapResult {
  SnapType type = SnapType::None;
  double x = 0;
  double y = 0;
  // Which entity produced it, for the on-canvas indicator and for tests.
  // -1 when the type has no single owner (Grid, None).
  int index = -1;
  // Intersection only: the second edge involved.
  int otherIndex = -1;

  bool snapped() const { return type != SnapType::None; }
};

// Human-readable name, for the indicator and for failure messages.
const char* name(SnapType type);

// The one entry point.
//
// captureRadius is in MODEL units -- the caller converts from pixels so
// the behaviour is zoom-independent. gridSize <= 0 disables grid snap
// regardless of the flag.
//
// referenceX/Y is where the current drawing operation started; it is what
// makes Perpendicular and Tangent meaningful. Pass hasReference=false
// when not drawing, and those two are skipped.
SnapResult findSnap(const FemmProblem& p, double cursorX, double cursorY,
    double captureRadius, unsigned flags, double gridSize,
    bool hasReference = false, double referenceX = 0, double referenceY = 0);

} // namespace SnapEngine
