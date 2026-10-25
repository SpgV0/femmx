#pragma once

// ContourSampler.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-10-25
// (issue #114).
//
// What Plot X-Y samples along a contour -- the quantities classic's
// GetLineValues (femm/FemmviewDoc.cpp) offers, with its conventions:
//
//   - the tangent t is each contour segment's direction and the normal
//     is n = i*t (t rotated +90 degrees); every sample is nudged 1e-6
//     along n, so a contour drawn ON a material boundary samples one
//     side of it rather than flickering between the two;
//   - magnetics' potential is plotted signed in DC (A, or flux when
//     axisymmetric) and as a magnitude in AC; a component (B.n, H.t...)
//     of an AC field is the magnitude of its complex value;
//   - with Smoothing on, field vectors are node-averaged within each
//     material and interpolated across the element -- the element-
//     constant field drew a staircase that was ~7% low near a wire.
//
// Magnetics: potential, |B|, B.n, B.t, |H|, H.n, H.t, and in AC |Je| and
// |Js+Je|. The other three: potential, then the flux (|D|, |F|, |J|) and
// the intensity (|E|, |G|) with their normal and tangential parts.

#include <QPointF>
#include <QString>
#include <QVector>

#include "ProblemKind.h"

struct FemmProblem;
struct MeshSolution;

namespace ContourSampler {

struct Series {
  QString label; // "B.n"
  QString unit;  // "T"
  QVector<double> values;
};

struct Result {
  QVector<double> arc; // along the contour, model length units
  QVector<double> x, y;
  QVector<bool> inside; // false where the sample fell outside the mesh
  QVector<Series> series;
  QString lengthUnit; // "mm"
};

Result sample(const MeshSolution& solution, const FemmProblem& problem, FemmProblemKind kind,
    bool smooth, const QVector<QPointF>& contour, int samples);

// The name of a model length unit for an axis title: "mm", "in", ...
QString lengthUnitName(const FemmProblem& problem);

} // namespace ContourSampler
