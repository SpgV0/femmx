#pragma once

#include <QHash>
#include <QVector>

struct FemmProblem;
struct FemmConstraint;

// Modified by Claude (Anthropic), noreply@anthropic.com: parametric
// geometric constraint solver -- per direct user request ("add
// dimensions when drawings and constraints similar to modern cad"). See
// FemmProblem.h's own comment on FemmConstraint/FemmDimension for scope
// (in-session drawing aid, not persisted to .fem/.femx). Solves the
// FemmProblem's current constraints/dimensions by adjusting whichever
// nodes they reference, via Levenberg-Marquardt (damped Gauss-Newton) on
// a finite-difference Jacobian -- see ConstraintSolver.cpp's own top
// comment and DenseMatrix.h for why this approach (rather than analytic
// derivatives or a full SVD) was chosen at this problem's scale. Stays
// Qt-graphics-free (only depends on FemmProblem.h), matching
// FemmProblemEdit's own scope -- GeometryScene owns all rendering.
namespace ConstraintSolver {

// Per-node classification after a solve, for GeometryScene's DOF-based
// coloring pass. A node with no entry is implicitly Unconstrained (not
// referenced by any constraint or dimension).
enum class SketchStatus {
  FullyConstrained, // this node's connected constraint sub-system has DOF==0, converged, full rank
  UnderConstrained, // DOF > 0, converged
  Redundant,        // rank-deficient relative to equation count, still converged
  Conflicting,      // this node's sub-system did not converge
};

struct SolveResult {
  bool converged = false;
  double finalResidualNorm = 0;
  int iterations = 0;
  QHash<int, SketchStatus> nodeStatus; // node index -> classification
};

// Solves every constraint/dimension currently in p.constraints/
// p.dimensions, adjusting p.nodes' x/y in place. A no-op, trivially-
// converged result if there are none. On failure to converge, reverts
// every touched node to its position before the call -- a failed solve
// leaves geometry unchanged, matching real CAD tools' own "solve
// failed" behavior, rather than leaving a half-adjusted mess.
SolveResult solve(FemmProblem& p);

// Modified by Claude (Anthropic), noreply@anthropic.com: exposed for
// GeometryScene's constraint glyphs -- per direct user request ("symbols
// indicating constraints than I can click and remove"). Returns the node
// indices referenced (directly, or via a segment/arc's own endpoints) by
// constraint `c` -- the same logic solve() itself uses to scope its
// unknowns, reused here so a glyph's live-follow-on-drag wiring can't
// drift out of sync with what the solver actually considers "this
// constraint's nodes".
QVector<int> touchedNodes(const FemmProblem& p, const FemmConstraint& c);

// A representative on-canvas position for constraint `c`'s glyph -- the
// average of its touched nodes' current positions. Output via x/y (not
// QPointF) to keep this header Qt-graphics-free, matching
// FemmProblemEdit's own scope.
void anchorPoint(const FemmProblem& p, const FemmConstraint& c, double& x, double& y);

} // namespace ConstraintSolver
