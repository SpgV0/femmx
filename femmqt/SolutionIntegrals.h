#pragma once

// SolutionIntegrals.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #83).
//
// The numbers a post-processor exists to produce: what the field is at a
// point, what flows through a line, and what a region integrates to.
//
// These are the same three operations in all four physics, over
// different quantities:
//
//   point     potential and field at (x, y)
//   line      the normal flux of the field through a polyline --
//             magnetic flux, electric flux, heat flow through a surface,
//             current through a cross-section
//   block     area, and the energy (or loss) stored in a region
//
// WHAT THE BLOCK INTEGRAL MEANS PER PHYSICS, since "energy" is the word
// used for three different things:
//
//   magnetics        |B|^2 / (2 mu)      stored magnetic energy, J/m
//   electrostatics   D.E / 2             stored electric energy, J/m
//   current flow     J.E / 2             resistive loss, W/m
//   heat flow        --                  no energy density; the useful
//                                        block quantity is average
//                                        temperature, and the useful
//                                        surface quantity is heat flow
//
// Per metre of depth in a planar problem, because that is what a 2D
// solution determines; multiplying by the problem's Depth is the
// caller's decision and is not done here.

#include <QString>
#include <QVector>

#include "SolutionField.h"
#include "SolutionFileIO.h"

struct FemmProblem;

namespace SolutionIntegrals {

struct PointValue {
  bool ok = false;
  int element = -1; // which element contained the point, or -1
  double potentialRe = 0, potentialIm = 0;
  SolutionField::Vector2 field;
};

// The element containing (x, y), or -1. Uses barycentric coordinates,
// so a point exactly on an edge belongs to one of the two elements
// sharing it rather than to neither.
int elementAt(const SolvedMesh& mesh, double x, double y);

// Potential interpolated linearly across the containing element, and
// that element's field. Coordinates are in the model's own units.
PointValue pointValue(const SolvedMesh& mesh, const FemmProblem& p, double x, double y);

struct BlockResult {
  bool ok = false;
  double area = 0;            // m^2
  double energy = 0;          // J/m, or W/m for current flow; 0 for heat flow
  double averagePotential = 0; // the stored scalar, area-weighted
  int elementCount = 0;
};

// Integrates over every element whose block label is in `labels`. An
// empty `labels` means every element.
BlockResult blockIntegral(const SolvedMesh& mesh, const FemmProblem& p,
    const QVector<int>& labels);

// The flux of the field through the polyline, taken with the normal
// obtained by rotating each segment's direction by -90 degrees, so a
// closed contour traversed counterclockwise gives outward-positive flux.
//
// Returns the real and imaginary parts separately: an AC magnetics or a
// current-flow field is complex, and adding the magnitudes of the two
// halves would be meaningless.
struct LineResult {
  bool ok = false;
  double fluxRe = 0, fluxIm = 0;
  double length = 0; // m
};

LineResult lineIntegral(const SolvedMesh& mesh, const FemmProblem& p,
    const QVector<QPair<double, double>>& polyline);

// What the block integral is called and measured in, for this physics.
// Empty name means the physics has no meaningful energy density -- heat
// flow -- and the caller should not offer it.
SolutionField::Quantity blockEnergyQuantity(FemmProblemKind kind);

// What a line integral of this physics' field represents.
SolutionField::Quantity lineFluxQuantity(FemmProblemKind kind);

} // namespace SolutionIntegrals
