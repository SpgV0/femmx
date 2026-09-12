#pragma once

// SketchTransform.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12
// (issue #32).
//
// What happens to constraints and dimensions when geometry is copied,
// rotated, mirrored or scaled.
//
// THE AUDIT #32 ASKED FOR, FIRST, because the answer changes what needed
// building. Measured against the code as it stood:
//
//   * copySelected, translateCopySelected and rotateCopySelected APPEND
//     their copies. Originals keep their indices, so every existing
//     constraint and dimension goes on referencing exactly the entity it
//     always did. The feared outcome -- "now pointing at the wrong
//     entity" -- did NOT happen. Nothing here was broken.
//
//   * The copies arrived with no constraints at all. A mirrored copy of
//     a fully constrained profile came back as dumb coordinates, which
//     is the gap this file fills.
//
//   * moveSelected, rotateSelected, mirrorSelected and scaleSelected
//     move geometry in place without touching any index, so references
//     stayed correct there too. But three of them change what a
//     constraint MEANS: a rotated line is no longer horizontal, and a
//     scaled profile no longer measures what its dimensions say. That
//     part was silently wrong.
//
// So: nothing to repair in the references, and two real things to build
// -- carry the sketch onto copies, and keep it honest through an
// in-place transform.
//
// THE ORIENTATION RULES, stated because the ticket asked for them to be
// explicit rather than emergent:
//
//   Rotation by a multiple of 90 degrees maps Horizontal and Vertical
//   onto each other (swapped at 90 and 270, unchanged at 0 and 180), so
//   they are rewritten. At any other angle a horizontal line simply is
//   not horizontal or vertical any more, and the constraint is dropped
//   from the copy and reported.
//
//   Mirroring a line of direction a across an axis of direction f gives
//   2f - a. Horizontal therefore survives as Horizontal when the axis is
//   itself horizontal or vertical, and becomes Vertical when the axis is
//   at 45 or 135 degrees. Every other axis leaves it neither, and it is
//   dropped and reported.
//
//   Everything else -- Coincident, Parallel, Perpendicular, Equal,
//   Tangent, Concentric, Symmetric -- is preserved by all four
//   transforms, because each is a statement about the relationship
//   between two entities and rigid motions and uniform scaling preserve
//   all of those.
//
//   Distance, HorizontalDistance, VerticalDistance and Radius dimensions
//   carry a LENGTH, so a scale multiplies their value. Angle and
//   AngleLines do not.
//
//   HorizontalDistance and VerticalDistance are lengths measured along
//   an axis, so a rotation is treated exactly like Horizontal and
//   Vertical: swapped at 90 and 270, dropped at any other angle.
//
// A constraint is copied only when EVERY entity it references was
// copied. Copying one whose other end was not would silently attach the
// copy to the original -- a constraint between the copy and the thing it
// was copied from, which nobody asked for and which the solver would
// enforce.

#include <QHash>
#include <QString>
#include <QStringList>

struct FemmProblem;

namespace SketchTransform {

// What the geometry did, so the rules above can be applied.
struct Motion {
  enum Type {
    Translate,
    Rotate,
    Mirror,
    Scale,
  };
  Type type = Translate;
  double angleDeg = 0; // Rotate: the rotation. Mirror: the AXIS's own direction.
  double factor = 1;   // Scale
};

struct Report {
  int constraintsCopied = 0;
  int dimensionsCopied = 0;
  int constraintsDropped = 0;
  int dimensionsAdjusted = 0;
  int conflictsIntroduced = 0;
  QStringList notes;

  QString summary() const;
};

// Copy every constraint and dimension whose referents were all copied,
// onto the copies. The maps are old index -> new index, exactly as the
// transform built them.
void copyOntoCopies(FemmProblem& p, const QHash<int, int>& nodeMap,
    const QHash<int, int>& segmentMap, const QHash<int, int>& arcMap,
    const Motion& motion, Report* report);

// After an IN-PLACE transform of the selection: rewrite the constraints
// whose meaning the motion changed, scale the dimension values a scale
// invalidated, and report the ones that now conflict with where the
// geometry ended up.
void adjustAfterInPlace(FemmProblem& p, const Motion& motion, Report* report);

} // namespace SketchTransform
