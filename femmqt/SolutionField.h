#pragma once

// SolutionField.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #83).
//
// What each physics plots, derived from what its solver actually stored.
//
// All four solvers store ONE scalar per mesh node -- A, V, T, V -- and
// nothing else. Every field a post-processor draws comes from the
// gradient of that scalar across each element, which is a single shared
// calculation over a linear triangle. Only what the gradient is then
// multiplied by differs:
//
//   magnetics        B = (dA/dy, -dA/dx)          curl of A
//   electrostatics   E = -grad V,  D = eps0*er*E
//   heat flow        G = -grad T,  F = k*G
//   current flow     E = -grad V,  J = sigma*E
//
// So this is one gradient and four short expressions, not four field
// solvers. The classic GUI computes each of these in its own
// post-processor (GetElementB, GetElementD, GetElementF, GetElementJ).
//
// UNITS ARE PART OF THE ANSWER, not decoration. The mesh is in the
// model's own length units and the material constants are in the units
// the file format uses -- conductivity in MS/m, permittivity relative,
// thermal conductivity in W/(m*K). A field that is right in shape and
// wrong by 1e6 looks entirely plausible on a plot with an auto-scaled
// legend, so each quantity below carries the unit it is actually in.

#include <QString>
#include <QVector>

#include "SolutionFileIO.h"

struct FemmProblem;
enum class FemmProblemKind;

namespace SolutionField {

// A complex planar vector. The imaginary halves are zero for anything
// but an AC magnetics or a current-flow solution.
struct Vector2 {
  double xRe = 0, xIm = 0;
  double yRe = 0, yIm = 0;

  // sqrt(|x|^2 + |y|^2), the magnitude a density plot shows.
  double magnitude() const;
};

// One plottable scalar, for the density-plot selector and the legend.
struct Quantity {
  QString name; // "Flux density |B|"
  QString unit; // "T"
};

// What this physics can plot, primary quantity first. The order is the
// order the classic post-processor's own plot menu uses.
QVector<Quantity> quantities(FemmProblemKind kind);

// What the file stores per node, named and united for this physics:
// magnetics "A" in Wb/m, electrostatics and current flow "V" in volts,
// heat flow "T" in kelvin.
Quantity potentialQuantity(FemmProblemKind kind);

// The gradient of the nodal potential across one element, in per-metre
// units. Shared by all four; this is the whole of what the solution file
// actually determines.
//
// Returns false for a degenerate (zero-area) element rather than
// dividing by zero -- triangle can emit them at sharp corners, and a
// NaN propagates silently into the plot's auto-range and flattens the
// whole legend.
bool potentialGradient(const SolvedMesh& mesh, const FemmProblem& p, int elementIndex,
    Vector2& gradient);

// The primary derived vector for this physics -- B, E, heat flux or
// current density -- for one element, in SI units.
//
// Needs the problem as well as the mesh because three of the four
// multiply the gradient by a material constant, which comes from the
// element's block label.
bool elementField(const SolvedMesh& mesh, const FemmProblem& p, int elementIndex,
    Vector2& field);

} // namespace SolutionField
