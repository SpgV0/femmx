#pragma once

// SolutionAdapter.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #83).
//
// Turns a SolvedMesh of any physics into the MeshSolution the existing
// renderer already paints.
//
// WHY AN ADAPTER RATHER THAN A REWRITE. SolutionView is 3700 lines of
// working, verified magnetics rendering -- density plots, contours, the
// legend, hover readout, auto-ranging with the small-element and
// external-region heuristics ported from the classic post-processor.
// Almost none of that is about magnetism: it paints a scalar per element
// and a potential per node. Rewriting it to be generic would put every
// one of those behaviours at risk to gain uniformity the renderer does
// not actually need.
//
// So the mapping is made explicit here instead, in one small place:
//
//   MeshSolutionNode::Are/Aim   <- the physics' stored potential
//                                  (A, V, T, V)
//   MeshSolutionElement::B1/B2  <- the physics' primary field vector
//                                  (B, D, heat flux, J)
//
// The renderer's "B" is therefore "whatever this physics' field is", and
// the LABELS are what must change per kind -- which is exactly what
// SolutionField::quantities provides. A plot whose legend says "T" while
// showing heat flux would be worse than no plot, so the naming is not
// cosmetic here.
//
// The magnetics-only fields of MeshSolutionElement -- muX/muY, sigma,
// the source current density, the BH-curve index -- are left at their
// defaults for the other three physics. They feed the H and J density
// options, which those physics do not offer; SolutionField::quantities
// is what decides which options a kind gets.

#include <QString>

struct FemmProblem;
struct MeshSolution;
struct SolvedMesh;

namespace SolutionAdapter {

// Fills `out` from `mesh`, deriving each element's field for p.kind.
//
// Returns false only if the mesh is unusable (no elements). Individual
// degenerate elements are kept with a zero field rather than dropped:
// removing them would renumber every element after, and the element
// indices are what the block labels and the hover readout refer to.
bool toMeshSolution(const SolvedMesh& mesh, const FemmProblem& p, MeshSolution& out);

} // namespace SolutionAdapter
