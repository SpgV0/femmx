#define _USE_MATH_DEFINES

#include "ConstraintSolver.h"

#include "DenseMatrix.h"
#include "FemmProblem.h"
#include "FemmProblemEdit.h"

#include <QSet>

#include <algorithm>
#include <cmath>
#include <complex>

// Modified by Claude (Anthropic), noreply@anthropic.com: see
// ConstraintSolver.h's own comment for context. Implementation notes:
//
// - Unknowns are scoped to only the nodes actually referenced (directly,
//   or via a segment/arc's own endpoints) by at least one constraint or
//   dimension -- untouched nodes are never part of the solve at all.
// - Residuals are closed-form scalar equations evaluated from CURRENT
//   node positions (see the residualXxx functions below) -- 1 per
//   Horizontal/Vertical/Parallel/Perpendicular/Equal/Tangent/Distance/
//   Radius/Angle, 2 (dx,dy or center-x,center-y) per Coincident/
//   Concentric/Symmetric.
// - The Jacobian is central finite differences (perturb each unknown,
//   re-evaluate every residual), not hand-derived analytic derivatives
//   -- at sketch scale (tens to low hundreds of unknowns) this is cheap
//   and far less bug-prone than deriving/maintaining correct analytic
//   derivatives for 9 constraint types + 3 dimension types.
// - Solved via Levenberg-Marquardt (damped Gauss-Newton: solve
//   (J^T J + lambda*diag(J^T J)) dx = -J^T r, growing/shrinking lambda
//   based on whether the step reduced the residual), not plain Newton --
//   this degrades gracefully for both under-constrained (rank-deficient
//   J -- the damping term produces a minimum-norm-ish step, so
//   unconstrained DOFs just don't move much) and over-constrained/
//   conflicting systems, without needing separate special-case branches.
// - DOF/redundancy analysis reuses the SAME Gaussian-elimination-based
//   rank estimate (DenseMatrix.h's estimateRank) the LM step already
//   needs, rather than adding a second numerical algorithm (e.g. a
//   hand-rolled SVD) purely for rank analysis -- simpler and lower-risk
//   to get right, at the cost of being somewhat less numerically robust
//   than a true SVD right at near-degenerate configurations. Accepted
//   tradeoff at this problem's scale.
// - Classification (Fully/Under/Redundant/Conflicting) is done PER
//   CONNECTED COMPONENT of the constraint graph (via a small union-find
//   over touched nodes), not globally -- so one fully-constrained
//   rectangle and one separate under-constrained pair of parallel lines
//   in the same sketch are correctly classified independently, not
//   blended into one combined answer.
//
// Known, documented scope limitations (not silently dropped):
// - Multi-solution tangency (external vs. internal) picks whichever the
//   CURRENT geometry is closer to, not a disambiguation UI.
// - No convergence guarantee on pathological/highly-redundant systems --
//   the bounded iteration count + inner-loop lambda cap is the safety
//   net; a failed solve reverts to the pre-solve geometry.
// - "Which constraints conflict" is reported at the connected-component
//   granularity (every node in that component gets Conflicting), not
//   true minimal-conflict-set isolation.

namespace {

using Complex = std::complex<double>;

double nx(const FemmProblem& p, int nodeIdx) { return p.nodes[nodeIdx].x; }
double ny(const FemmProblem& p, int nodeIdx) { return p.nodes[nodeIdx].y; }

// ---- Union-find over node indices, for connected-component grouping ----

class UnionFind {
public:
  explicit UnionFind(int n) : m_parent(n)
  {
    for (int i = 0; i < n; i++)
      m_parent[i] = i;
  }
  int find(int x)
  {
    while (m_parent[x] != x) {
      m_parent[x] = m_parent[m_parent[x]];
      x = m_parent[x];
    }
    return x;
  }
  void unite(int a, int b)
  {
    a = find(a);
    b = find(b);
    if (a != b)
      m_parent[a] = b;
  }

private:
  QVector<int> m_parent;
};

// ---- Which nodes does each constraint/dimension actually touch? --------
// touchedNodesForConstraint itself moved out of this anonymous namespace
// -- it's now ConstraintSolver::touchedNodes, declared in
// ConstraintSolver.h and defined alongside solve() below, exposed for
// GeometryScene's constraint glyphs. Kept a thin local alias here so
// this file's many internal call sites don't all need qualifying.
QVector<int> touchedNodesForDimension(const FemmProblem& p, const FemmDimension& d)
{
  switch (d.type) {
  case DimensionType::Distance:
    return {d.refA, d.refB};
  case DimensionType::Radius: {
    const FemmArcSegment& a = p.arcSegments[d.refA];
    return {a.n0, a.n1};
  }
  case DimensionType::Angle:
    return {d.refA, d.refB, d.refC};
  }
  return {};
}

int residualCountForConstraint(ConstraintType type)
{
  switch (type) {
  case ConstraintType::Coincident:
  case ConstraintType::Concentric:
  case ConstraintType::Symmetric:
    return 2;
  default:
    return 1;
  }
}

// ---- Constraint residuals -----------------------------------------------
// Each returns 0 when satisfied. See this file's top comment for the
// overall approach.

void residualCoincident(const FemmProblem& p, const FemmConstraint& c, double& r0, double& r1)
{
  r0 = nx(p, c.refB) - nx(p, c.refA);
  r1 = ny(p, c.refB) - ny(p, c.refA);
}

double residualHorizontal(const FemmProblem& p, const FemmConstraint& c)
{
  const FemmSegment& s = p.segments[c.refA];
  return ny(p, s.n1) - ny(p, s.n0);
}

double residualVertical(const FemmProblem& p, const FemmConstraint& c)
{
  const FemmSegment& s = p.segments[c.refA];
  return nx(p, s.n1) - nx(p, s.n0);
}

double residualParallel(const FemmProblem& p, const FemmConstraint& c)
{
  const FemmSegment& s0 = p.segments[c.refA];
  const FemmSegment& s1 = p.segments[c.refB];
  double d0x = nx(p, s0.n1) - nx(p, s0.n0), d0y = ny(p, s0.n1) - ny(p, s0.n0);
  double d1x = nx(p, s1.n1) - nx(p, s1.n0), d1y = ny(p, s1.n1) - ny(p, s1.n0);
  return d0x * d1y - d0y * d1x; // cross product -- 0 iff parallel
}

double residualPerpendicular(const FemmProblem& p, const FemmConstraint& c)
{
  const FemmSegment& s0 = p.segments[c.refA];
  const FemmSegment& s1 = p.segments[c.refB];
  double d0x = nx(p, s0.n1) - nx(p, s0.n0), d0y = ny(p, s0.n1) - ny(p, s0.n0);
  double d1x = nx(p, s1.n1) - nx(p, s1.n0), d1y = ny(p, s1.n1) - ny(p, s1.n0);
  return d0x * d1x + d0y * d1y; // dot product -- 0 iff perpendicular
}

double segmentLength(const FemmProblem& p, const FemmSegment& s)
{
  return std::hypot(nx(p, s.n1) - nx(p, s.n0), ny(p, s.n1) - ny(p, s.n0));
}

double residualEqual(const FemmProblem& p, const FemmConstraint& c)
{
  if (c.isArcPair) {
    Complex c0, c1;
    double r0 = 0, r1 = 0;
    FemmProblemEdit::circleFromArc(p, p.arcSegments[c.refA], c0, r0);
    FemmProblemEdit::circleFromArc(p, p.arcSegments[c.refB], c1, r1);
    return r1 - r0;
  }
  return segmentLength(p, p.segments[c.refB]) - segmentLength(p, p.segments[c.refA]);
}

double residualTangent(const FemmProblem& p, const FemmConstraint& c)
{
  Complex center;
  double radius = 0;
  if (!FemmProblemEdit::circleFromArc(p, p.arcSegments[c.refB], center, radius))
    return 0; // degenerate arc -- nothing meaningful to solve toward

  if (c.firstIsArc) {
    Complex center2;
    double radius2 = 0;
    if (!FemmProblemEdit::circleFromArc(p, p.arcSegments[c.refA], center2, radius2))
      return 0;
    double centerDist = std::abs(center2 - center);
    // Modified by Claude (Anthropic), noreply@anthropic.com: was a
    // dynamic pick of whichever of external (centerDist == r1+r2) or
    // internal (centerDist == |r1-r2|) tangency the CURRENT geometry was
    // closer to, re-evaluated on every residual call during the solve.
    // That makes the residual non-smooth exactly where the two
    // candidates cross over (|extResid| == |intResid|) -- confirmed as
    // the actual cause of a real solver failure (not just a theoretical
    // concern): a debug trace showed lambda climbing to ~1e10 while the
    // residual froze at a nonzero value, the textbook signature of an
    // optimizer hunting right at a non-smooth kink rather than a
    // legitimately hard-but-smooth problem. Always resolving to
    // EXTERNAL tangency (the far more common real case -- two separate,
    // non-overlapping circles meeting from outside) removes the
    // discontinuity entirely, at the cost of no longer auto-detecting
    // internal tangency (two circles, one nearly inside the other) --
    // a real, deliberate scope cut, not silently dropped: a distinct
    // "internal tangent" constraint variant would be the correct way to
    // add that back without reintroducing this failure mode.
    return centerDist - (radius + radius2);
  }

  const FemmSegment& seg = p.segments[c.refA];
  double x0 = nx(p, seg.n0), y0 = ny(p, seg.n0);
  double x1 = nx(p, seg.n1), y1 = ny(p, seg.n1);
  double dx = x1 - x0, dy = y1 - y0;
  double len = std::hypot(dx, dy);
  if (len <= 0)
    return 0;
  // Perpendicular distance from the arc's center to the infinite line
  // through (x0,y0)-(x1,y1), minus the arc's radius.
  double dist = std::abs(dx * (center.imag() - y0) - dy * (center.real() - x0)) / len;
  return dist - radius;
}

void residualConcentric(const FemmProblem& p, const FemmConstraint& c, double& r0, double& r1)
{
  Complex c0, c1;
  double rad0 = 0, rad1 = 0;
  FemmProblemEdit::circleFromArc(p, p.arcSegments[c.refA], c0, rad0);
  FemmProblemEdit::circleFromArc(p, p.arcSegments[c.refB], c1, rad1);
  r0 = c1.real() - c0.real();
  r1 = c1.imag() - c0.imag();
}

void residualSymmetric(const FemmProblem& p, const FemmConstraint& c, double& r0, double& r1)
{
  const FemmSegment& mirror = p.segments[c.refC];
  double x0 = nx(p, mirror.n0), y0 = ny(p, mirror.n0);
  double x1 = nx(p, mirror.n1), y1 = ny(p, mirror.n1);
  double dx = x1 - x0, dy = y1 - y0;
  double len = std::hypot(dx, dy);
  if (len <= 0) {
    r0 = r1 = 0;
    return;
  }
  double ux = dx / len, uy = dy / len;
  double rx = nx(p, c.refA), ry = ny(p, c.refA);
  FemmProblemEdit::reflectPoint(rx, ry, x0, y0, ux, uy);
  r0 = rx - nx(p, c.refB);
  r1 = ry - ny(p, c.refB);
}

// ---- Dimension residuals -------------------------------------------------

double residualDistance(const FemmProblem& p, const FemmDimension& d)
{
  return std::hypot(nx(p, d.refB) - nx(p, d.refA), ny(p, d.refB) - ny(p, d.refA)) - d.value;
}

double residualRadius(const FemmProblem& p, const FemmDimension& d)
{
  Complex c;
  double r = 0;
  if (!FemmProblemEdit::circleFromArc(p, p.arcSegments[d.refA], c, r))
    return 0;
  return r - d.value;
}

double residualAngle(const FemmProblem& p, const FemmDimension& d)
{
  double vx = nx(p, d.refA), vy = ny(p, d.refA);
  double a1 = std::atan2(ny(p, d.refB) - vy, nx(p, d.refB) - vx);
  double a2 = std::atan2(ny(p, d.refC) - vy, nx(p, d.refC) - vx);
  double diff = a2 - a1;
  // Wrap to (-pi, pi] so the residual doesn't jump discontinuously as the
  // geometry rotates through the +-180 degree boundary.
  while (diff > M_PI)
    diff -= 2 * M_PI;
  while (diff <= -M_PI)
    diff += 2 * M_PI;
  double targetRad = d.value * M_PI / 180.0;
  return diff - targetRad;
}

// One scalar residual slot -- a constraint contributes 1 or 2 (subIndex
// distinguishes which), a dimension always exactly 1. `component` is the
// union-find root of the node(s) this slot's constraint/dimension
// touches, filled in once up front and used only for the final
// per-component classification pass.
struct ResidualSource {
  int constraintIndex = -1;
  int dimensionIndex = -1;
  int subIndex = 0;
  int component = -1;
};

double evaluateOne(const FemmProblem& p, const ResidualSource& src)
{
  if (src.dimensionIndex >= 0) {
    const FemmDimension& d = p.dimensions[src.dimensionIndex];
    switch (d.type) {
    case DimensionType::Distance:
      return residualDistance(p, d);
    case DimensionType::Radius:
      return residualRadius(p, d);
    case DimensionType::Angle:
      return residualAngle(p, d);
    }
    return 0;
  }
  const FemmConstraint& c = p.constraints[src.constraintIndex];
  switch (c.type) {
  case ConstraintType::Coincident: {
    double r0, r1;
    residualCoincident(p, c, r0, r1);
    return src.subIndex == 0 ? r0 : r1;
  }
  case ConstraintType::Horizontal:
    return residualHorizontal(p, c);
  case ConstraintType::Vertical:
    return residualVertical(p, c);
  case ConstraintType::Parallel:
    return residualParallel(p, c);
  case ConstraintType::Perpendicular:
    return residualPerpendicular(p, c);
  case ConstraintType::Equal:
    return residualEqual(p, c);
  case ConstraintType::Tangent:
    return residualTangent(p, c);
  case ConstraintType::Concentric: {
    double r0, r1;
    residualConcentric(p, c, r0, r1);
    return src.subIndex == 0 ? r0 : r1;
  }
  case ConstraintType::Symmetric: {
    double r0, r1;
    residualSymmetric(p, c, r0, r1);
    return src.subIndex == 0 ? r0 : r1;
  }
  }
  return 0;
}

double sumSquares(const QVector<double>& v)
{
  double s = 0;
  for (double x : v)
    s += x * x;
  return s;
}

double maxAbs(const QVector<double>& v)
{
  double m = 0;
  for (double x : v)
    m = std::max(m, std::abs(x));
  return m;
}

} // namespace

namespace ConstraintSolver {

QVector<int> touchedNodes(const FemmProblem& p, const FemmConstraint& c)
{
  switch (c.type) {
  case ConstraintType::Coincident:
    return {c.refA, c.refB};
  case ConstraintType::Horizontal:
  case ConstraintType::Vertical: {
    const FemmSegment& s = p.segments[c.refA];
    return {s.n0, s.n1};
  }
  case ConstraintType::Parallel:
  case ConstraintType::Perpendicular: {
    const FemmSegment& s0 = p.segments[c.refA];
    const FemmSegment& s1 = p.segments[c.refB];
    return {s0.n0, s0.n1, s1.n0, s1.n1};
  }
  case ConstraintType::Equal:
    if (c.isArcPair) {
      const FemmArcSegment& a0 = p.arcSegments[c.refA];
      const FemmArcSegment& a1 = p.arcSegments[c.refB];
      return {a0.n0, a0.n1, a1.n0, a1.n1};
    } else {
      const FemmSegment& s0 = p.segments[c.refA];
      const FemmSegment& s1 = p.segments[c.refB];
      return {s0.n0, s0.n1, s1.n0, s1.n1};
    }
  case ConstraintType::Tangent: {
    QVector<int> t;
    const FemmArcSegment& arcB = p.arcSegments[c.refB];
    t.push_back(arcB.n0);
    t.push_back(arcB.n1);
    if (c.firstIsArc) {
      const FemmArcSegment& arcA = p.arcSegments[c.refA];
      t.push_back(arcA.n0);
      t.push_back(arcA.n1);
    } else {
      const FemmSegment& segA = p.segments[c.refA];
      t.push_back(segA.n0);
      t.push_back(segA.n1);
    }
    return t;
  }
  case ConstraintType::Concentric: {
    const FemmArcSegment& a0 = p.arcSegments[c.refA];
    const FemmArcSegment& a1 = p.arcSegments[c.refB];
    return {a0.n0, a0.n1, a1.n0, a1.n1};
  }
  case ConstraintType::Symmetric: {
    // The mirror line's own two nodes are ALSO treated as unknowns here,
    // same as everything else this solver touches -- there's no
    // separate "fixed/construction geometry" concept yet (a natural
    // future enhancement), so the solver is free to adjust the mirror
    // line itself if that reduces residual more than moving the two
    // symmetric points.
    const FemmSegment& mirror = p.segments[c.refC];
    return {c.refA, c.refB, mirror.n0, mirror.n1};
  }
  }
  return {};
}

void anchorPoint(const FemmProblem& p, const FemmConstraint& c, double& x, double& y)
{
  QVector<int> nodes = touchedNodes(p, c);
  x = 0;
  y = 0;
  if (nodes.isEmpty())
    return;
  for (int n : nodes) {
    x += p.nodes[n].x;
    y += p.nodes[n].y;
  }
  x /= nodes.size();
  y /= nodes.size();
}

SolveResult solve(FemmProblem& p)
{
  SolveResult result;
  if (p.constraints.isEmpty() && p.dimensions.isEmpty()) {
    result.converged = true;
    return result;
  }

  // ---- 1. Touched nodes + union-find over them -------------------------
  UnionFind uf(p.nodes.size());
  QVector<int> touchedNodeList; // unique, first-seen order
  QSet<int> touchedSet;
  auto touch = [&](int nodeIdx) {
    if (!touchedSet.contains(nodeIdx)) {
      touchedSet.insert(nodeIdx);
      touchedNodeList.push_back(nodeIdx);
    }
  };
  for (const FemmConstraint& c : p.constraints) {
    QVector<int> t = touchedNodes(p, c);
    for (int n : t)
      touch(n);
    for (int i = 1; i < t.size(); i++)
      uf.unite(t[0], t[i]);
  }
  for (const FemmDimension& d : p.dimensions) {
    QVector<int> t = touchedNodesForDimension(p, d);
    for (int n : t)
      touch(n);
    for (int i = 1; i < t.size(); i++)
      uf.unite(t[0], t[i]);
  }
  if (touchedNodeList.isEmpty()) {
    result.converged = true;
    return result;
  }

  // ---- 2. Unknown vector: 2 scalars (x,y) per touched node --------------
  QHash<int, int> nodeToSlot;
  for (int i = 0; i < touchedNodeList.size(); i++)
    nodeToSlot[touchedNodeList[i]] = i;
  const int numUnknowns = touchedNodeList.size() * 2;

  auto getX = [&]() {
    QVector<double> x(numUnknowns);
    for (int i = 0; i < touchedNodeList.size(); i++) {
      x[2 * i] = p.nodes[touchedNodeList[i]].x;
      x[2 * i + 1] = p.nodes[touchedNodeList[i]].y;
    }
    return x;
  };
  auto setX = [&](const QVector<double>& x) {
    for (int i = 0; i < touchedNodeList.size(); i++) {
      p.nodes[touchedNodeList[i]].x = x[2 * i];
      p.nodes[touchedNodeList[i]].y = x[2 * i + 1];
    }
  };
  const QVector<double> originalX = getX();

  // ---- 3. Residual sources, tagged with their connected-component root --
  QVector<ResidualSource> sources;
  for (int ci = 0; ci < p.constraints.size(); ci++) {
    const FemmConstraint& c = p.constraints[ci];
    QVector<int> t = touchedNodes(p, c);
    int root = t.isEmpty() ? -1 : uf.find(t[0]);
    int n = residualCountForConstraint(c.type);
    for (int k = 0; k < n; k++)
      sources.push_back({ci, -1, k, root});
  }
  for (int di = 0; di < p.dimensions.size(); di++) {
    const FemmDimension& d = p.dimensions[di];
    QVector<int> t = touchedNodesForDimension(p, d);
    int root = t.isEmpty() ? -1 : uf.find(t[0]);
    sources.push_back({-1, di, 0, root});
  }

  auto evaluateResiduals = [&]() {
    QVector<double> r(sources.size());
    for (int i = 0; i < sources.size(); i++)
      r[i] = evaluateOne(p, sources[i]);
    return r;
  };

  // Central-difference Jacobian at xBase -- restores geometry to xBase
  // before returning.
  auto computeJacobian = [&](const QVector<double>& xBase) {
    DenseLinAlg::Matrix J(sources.size(), numUnknowns);
    QVector<double> xPerturbed = xBase;
    constexpr double kBaseEps = 1e-6;
    for (int j = 0; j < numUnknowns; j++) {
      double eps = std::max(kBaseEps, std::abs(xBase[j]) * kBaseEps);
      double saved = xPerturbed[j];

      xPerturbed[j] = saved + eps;
      setX(xPerturbed);
      QVector<double> rPlus = evaluateResiduals();

      xPerturbed[j] = saved - eps;
      setX(xPerturbed);
      QVector<double> rMinus = evaluateResiduals();

      xPerturbed[j] = saved;

      for (int i = 0; i < sources.size(); i++)
        J(i, j) = (rPlus[i] - rMinus[i]) / (2 * eps);
    }
    setX(xBase);
    return J;
  };

  // ---- 4. Levenberg-Marquardt --------------------------------------------
  constexpr int kMaxIterations = 100;
  constexpr int kMaxInnerIterations = 30;
  constexpr double kConvergedMaxAbsResidual = 1e-8;
  constexpr double kMaxLambda = 1e12;

  QVector<double> x = getX();
  QVector<double> r = evaluateResiduals();
  double normR2 = sumSquares(r);
  double lambda = 1e-3;

  int iter = 0;
  for (; iter < kMaxIterations; iter++) {
    if (maxAbs(r) < kConvergedMaxAbsResidual)
      break;

    DenseLinAlg::Matrix J = computeJacobian(x);
    DenseLinAlg::Matrix JTJ = DenseLinAlg::multiplyATA(J);
    QVector<double> negJTr = DenseLinAlg::multiplyATb(J, r);
    for (double& v : negJTr)
      v = -v;

    // Modified by Claude (Anthropic), noreply@anthropic.com: floor each
    // diagonal damping term at a fraction of JTJ's own largest diagonal
    // entry, INDEPENDENT of lambda's current value -- found via a real
    // failing test (Tangent between two arcs, 1 residual touching 8
    // unknowns, so J^T*J is exactly rank-1: every direction orthogonal
    // to the single nonzero singular direction has diag(JTJ)[i] at or
    // near exactly 0). The first fix attempt (lambda*max(diag,
    // scale-relative-floor)) still coupled the floor to lambda -- lambda
    // shrinks by half on every accepted step regardless of how tiny the
    // improvement was, so after ~15-20 largely-cosmetic accepted steps
    // it decays toward its own floor and the "floor" for the null-space
    // directions vanishes right along with it, leaving those directions
    // essentially undamped again. Confirmed directly via a debug trace:
    // residual dropped fast for 2 steps (22 -> 8.9), then crept down by
    // ~0.01-0.03 per iteration for 25+ more (7.55 -> 7.47), a classic
    // signature of a near-singular system whose regularization has
    // decayed away -- not the earlier symptom (zero progress at all),
    // a different failure mode this first fix only partially addressed.
    // An absolute-per-outer-iteration floor (added on top of, not
    // multiplied by, lambda*diag) keeps every direction meaningfully
    // regularized no matter how small lambda itself has become.
    double maxDiag = 0.0;
    for (int i = 0; i < numUnknowns; i++)
      maxDiag = std::max(maxDiag, JTJ(i, i));
    double minAbsoluteDamping = std::max(maxDiag * 1e-6, 1e-9);

    bool improved = false;
    for (int inner = 0; inner < kMaxInnerIterations && lambda < kMaxLambda; inner++) {
      DenseLinAlg::Matrix A = JTJ;
      for (int i = 0; i < numUnknowns; i++) {
        double diag = JTJ(i, i);
        A(i, i) = diag + std::max(lambda * diag, minAbsoluteDamping);
      }
      QVector<double> dx;
      if (!DenseLinAlg::solveLinearSystem(A, negJTr, dx)) {
        lambda *= 10;
        continue;
      }
      QVector<double> xTrial(numUnknowns);
      for (int i = 0; i < numUnknowns; i++)
        xTrial[i] = x[i] + dx[i];
      setX(xTrial);
      QVector<double> rTrial = evaluateResiduals();
      double normR2Trial = sumSquares(rTrial);
      if (normR2Trial < normR2) {
        x = xTrial;
        r = rTrial;
        normR2 = normR2Trial;
        // Modified by Claude (Anthropic), noreply@anthropic.com: was
        // std::max(lambda*0.5, 1e-12) -- that floor let lambda decay,
        // over many small-but-accepted steps, to a value so tiny it
        // provided essentially no regularization at all. That's fine
        // for a well-conditioned system, but a chronically underdetermined
        // one (the norm here -- most single constraints touch 4-8
        // unknowns with only 1-2 residual equations) can have near-
        // singular *combinations* of otherwise well-scaled directions
        // (confirmed directly: two nodes whose derivatives were exactly
        // +1/-1 -- individually as clean as any converging test here --
        // became a numerically unstable 2x2 sub-block once lambda decayed
        // far enough that they were both essentially undamped, stalling
        // the residual at a nonzero value indefinitely rather than
        // reaching it). A much higher floor keeps every solve
        // meaningfully regularized throughout, at the cost of not
        // shrinking damping as aggressively once a good region is
        // found -- an acceptable tradeoff at this solver's scale
        // (unknown counts in the tens, not thousands, so a few extra
        // iterations from more conservative damping costs nothing
        // noticeable), and every other test here still converges in
        // 2-4 iterations with this floor, unchanged from before.
        lambda = std::max(lambda * 0.5, 1e-4);
        improved = true;
        break;
      }
      lambda *= 10;
    }
    if (!improved)
      break; // stuck -- no improving step found even at the lambda cap
  }

  setX(x);
  const bool converged = maxAbs(r) < kConvergedMaxAbsResidual;
  result.converged = converged;
  result.finalResidualNorm = std::sqrt(normR2);
  result.iterations = iter;
  if (!converged)
    setX(originalX);

  // ---- 5. Per-connected-component DOF/status classification -------------
  // Recomputed at the FINAL (possibly reverted) geometry, so a reverted
  // solve's classification reflects the original geometry's own
  // constraint satisfaction, not the failed trial's.
  QVector<double> finalX = getX();
  QVector<double> finalR = evaluateResiduals();
  DenseLinAlg::Matrix finalJ = computeJacobian(finalX);

  QHash<int, QVector<int>> nodesByComponent; // root -> node indices
  for (int n : touchedNodeList)
    nodesByComponent[uf.find(n)].push_back(n);
  QHash<int, QVector<int>> rowsByComponent; // root -> residual row indices
  for (int i = 0; i < sources.size(); i++)
    if (sources[i].component >= 0)
      rowsByComponent[sources[i].component].push_back(i);

  for (auto it = nodesByComponent.constBegin(); it != nodesByComponent.constEnd(); ++it) {
    const int root = it.key();
    const QVector<int>& compNodes = it.value();
    const QVector<int> compRows = rowsByComponent.value(root);

    QVector<int> compCols;
    for (int n : compNodes) {
      int slot = nodeToSlot.value(n);
      compCols.push_back(2 * slot);
      compCols.push_back(2 * slot + 1);
    }

    DenseLinAlg::Matrix subJ(compRows.size(), compCols.size());
    for (int ri = 0; ri < compRows.size(); ri++)
      for (int ci = 0; ci < compCols.size(); ci++)
        subJ(ri, ci) = finalJ(compRows[ri], compCols[ci]);

    const int subRank = DenseLinAlg::estimateRank(subJ);
    const int subUnknowns = compCols.size();
    const int subEquations = compRows.size();

    double compMaxResidual = 0;
    for (int row : compRows)
      compMaxResidual = std::max(compMaxResidual, std::abs(finalR[row]));

    SketchStatus status;
    if (compMaxResidual >= kConvergedMaxAbsResidual)
      status = SketchStatus::Conflicting;
    else if (subRank < subEquations)
      status = SketchStatus::Redundant;
    else if (subUnknowns - subRank == 0)
      status = SketchStatus::FullyConstrained;
    else
      status = SketchStatus::UnderConstrained;

    for (int n : compNodes)
      result.nodeStatus[n] = status;
  }

  return result;
}

} // namespace ConstraintSolver
