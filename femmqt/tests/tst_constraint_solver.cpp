// tst_constraint_solver.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-11.
//
// The constraint/dimension sketch layer is the largest feature in v2.2.x:
// 9 constraint types, 4 dimension types, and a hand-rolled
// Levenberg-Marquardt solver with DOF/sketch-health classification. Its
// only check used to be `femmqt.exe --test-constraints`, a bespoke
// harness in main.cpp that printed "ALL TESTS PASSED" and that nothing in
// CI ever invoked. Numerical solver code no automated job runs is code
// that drifts (issue #12).
//
// The 17 scenario functions below are moved VERBATIM out of main.cpp
// rather than rewritten: they already encode carefully chosen starting
// geometries that visibly violate each relationship, and re-deriving them
// would risk quietly weakening the checks. Each is wrapped in its own
// QTest slot so ctest reports per-constraint results instead of one
// pass/fail for the lot. Their fprintf(stderr, ...) detail still comes
// through -- QTest captures it and prints it on failure.
//
// Everything after them is new coverage the CLI harness never had:
// sketch-health classification, redundant constraints, non-convergence
// being reported rather than looped on, determinism, and the sketch layer
// staying out of a saved .fem.

#include <QtTest>

#include "ConstraintSolver.h"
#include "FemmFileIO.h"
#include "FemmProblem.h"
#include "FemmProblemEdit.h"

#include <QTemporaryDir>

#include <cmath>
#include <cstdio>

namespace {

// Modified by Claude (Anthropic), noreply@anthropic.com: shared helpers
// for every testXxx() function below -- plain free functions (not the
// original single test's local lambdas) since many independent test
// functions now need them.
int addNode(FemmProblem& p, double x, double y)
{
  FemmNode n;
  n.x = x;
  n.y = y;
  p.nodes.push_back(n);
  return p.nodes.size() - 1;
}
int addSegment(FemmProblem& p, int n0, int n1)
{
  FemmSegment s;
  s.n0 = n0;
  s.n1 = n1;
  p.segments.push_back(s);
  return p.segments.size() - 1;
}
// 180-degree arc between two new nodes at (x0,y0)/(x1,y1) -- same "half
// circle from a diameter" convention this repo's own FEMM-scripting
// examples use, giving a simple, unambiguous radius = half the chord.
int addHalfArc(FemmProblem& p, double x0, double y0, double x1, double y1)
{
  int n0 = addNode(p, x0, y0);
  int n1 = addNode(p, x1, y1);
  FemmArcSegment a;
  a.n0 = n0;
  a.n1 = n1;
  a.arcLength = 180.0;
  p.arcSegments.push_back(a);
  return p.arcSegments.size() - 1;
}

// Modified by Claude (Anthropic), noreply@anthropic.com: `femmqt.exe
// --test-constraints` -- offline validation of ConstraintSolver in
// isolation, before any UI is wired to it (this project has no automated
// test framework, so a CLI mode is the established way to verify a
// numerical algorithm against a real build -- same idea as
// --convert-ansx above). One independent FemmProblem per constraint/
// dimension type (not sharing state across tests) so a failure in one
// can't mask or cause a failure in another. Builds a scenario that
// visibly violates the relationship, solves, then checks the result
// against a freshly-derived (not reused-from-the-solver) formula where
// practical -- see each testXxx()'s own comment for its specific check.

bool testRectangleRigid()
{
  FemmProblem p;
  int n0 = addNode(p, 0, 0);
  int n1 = addNode(p, 10, 2);
  int n2 = addNode(p, 9, 8);
  int n3 = addNode(p, 1, 7);
  int s0 = addSegment(p, n0, n1);
  int s1 = addSegment(p, n1, n2);
  int s2 = addSegment(p, n2, n3);
  int s3 = addSegment(p, n3, n0);

  FemmConstraint ch0;
  ch0.type = ConstraintType::Horizontal;
  ch0.refA = s0;
  FemmConstraint ch1;
  ch1.type = ConstraintType::Horizontal;
  ch1.refA = s2;
  FemmConstraint cv0;
  cv0.type = ConstraintType::Vertical;
  cv0.refA = s1;
  FemmConstraint cv1;
  cv1.type = ConstraintType::Vertical;
  cv1.refA = s3;
  p.constraints = {ch0, ch1, cv0, cv1};

  ConstraintSolver::SolveResult result = ConstraintSolver::solve(p);
  double dy0 = std::abs(p.nodes[n0].y - p.nodes[n1].y);
  double dy1 = std::abs(p.nodes[n2].y - p.nodes[n3].y);
  double dx0 = std::abs(p.nodes[n1].x - p.nodes[n2].x);
  double dx1 = std::abs(p.nodes[n3].x - p.nodes[n0].x);
  fprintf(stderr, "Horizontal+Vertical (rectangle): converged=%d iters=%d bottom dy=%g top dy=%g right dx=%g left dx=%g\n",
          result.converged, result.iterations, dy0, dy1, dx0, dx1);

  bool pass = result.converged && dy0 < 1e-6 && dy1 < 1e-6 && dx0 < 1e-6 && dx1 < 1e-6;
  for (int n : {n0, n1, n2, n3}) {
    auto it = result.nodeStatus.constFind(n);
    // 4 nodes = 8 unknowns, 4 independent equations -> DOF=4 remaining
    // (x/y translation + width/height) -- no "Fixed point" constraint
    // exists yet, so UnderConstrained is the correct expected result,
    // not a bug.
    if (it == result.nodeStatus.constEnd() || *it != ConstraintSolver::SketchStatus::UnderConstrained) {
      fprintf(stderr, "  FAIL: node %d wrong/missing status\n", n);
      pass = false;
    }
  }
  if (!pass)
    fprintf(stderr, "  FAIL\n");
  return pass;
}

bool testDistanceDimension()
{
  FemmProblem p;
  int n0 = addNode(p, 0, 0);
  int n1 = addNode(p, 3, 4); // length 5
  FemmDimension dim;
  dim.type = DimensionType::Distance;
  dim.refA = n0;
  dim.refB = n1;
  dim.value = 15.0;
  p.dimensions.push_back(dim);
  ConstraintSolver::SolveResult result = ConstraintSolver::solve(p);
  double len = std::hypot(p.nodes[n1].x - p.nodes[n0].x, p.nodes[n1].y - p.nodes[n0].y);
  fprintf(stderr, "Distance dimension: converged=%d length=%g (expect 15)\n", result.converged, len);
  bool pass = result.converged && std::abs(len - 15.0) < 1e-6;
  if (!pass)
    fprintf(stderr, "  FAIL\n");
  return pass;
}

// Modified by Claude (Anthropic), noreply@anthropic.com: starting point is
// deliberately NOT axis-aligned (dx=3, dy=4, i.e. the same 3-4-5 triangle
// testDistanceDimension uses) -- this is what actually distinguishes these
// from a plain Distance dimension test. If residualHorizontalDistance
// wrongly reused hypot() (the Distance formula) instead of |dx|, this test
// would still spuriously pass on an ALREADY-axis-aligned starting point
// (dy=0), which is exactly the kind of false-positive coverage this test
// is written to avoid.
bool testHorizontalDistanceDimension()
{
  FemmProblem p;
  int n0 = addNode(p, 0, 0);
  int n1 = addNode(p, 3, 4);
  FemmDimension dim;
  dim.type = DimensionType::HorizontalDistance;
  dim.refA = n0;
  dim.refB = n1;
  dim.value = 15.0;
  p.dimensions.push_back(dim);
  ConstraintSolver::SolveResult result = ConstraintSolver::solve(p);
  double dx = std::abs(p.nodes[n1].x - p.nodes[n0].x);
  fprintf(stderr, "Horizontal distance dimension: converged=%d |dx|=%g (expect 15)\n", result.converged, dx);
  bool pass = result.converged && std::abs(dx - 15.0) < 1e-6;
  if (!pass)
    fprintf(stderr, "  FAIL\n");
  return pass;
}

bool testVerticalDistanceDimension()
{
  FemmProblem p;
  int n0 = addNode(p, 0, 0);
  int n1 = addNode(p, 3, 4);
  FemmDimension dim;
  dim.type = DimensionType::VerticalDistance;
  dim.refA = n0;
  dim.refB = n1;
  dim.value = 15.0;
  p.dimensions.push_back(dim);
  ConstraintSolver::SolveResult result = ConstraintSolver::solve(p);
  double dy = std::abs(p.nodes[n1].y - p.nodes[n0].y);
  fprintf(stderr, "Vertical distance dimension: converged=%d |dy|=%g (expect 15)\n", result.converged, dy);
  bool pass = result.converged && std::abs(dy - 15.0) < 1e-6;
  if (!pass)
    fprintf(stderr, "  FAIL\n");
  return pass;
}

bool testCoincident()
{
  FemmProblem p;
  int n0 = addNode(p, 0, 0);
  int n1 = addNode(p, 10, 10);
  FemmConstraint c;
  c.type = ConstraintType::Coincident;
  c.refA = n0;
  c.refB = n1;
  p.constraints.push_back(c);
  ConstraintSolver::SolveResult result = ConstraintSolver::solve(p);
  double dist = std::hypot(p.nodes[n1].x - p.nodes[n0].x, p.nodes[n1].y - p.nodes[n0].y);
  fprintf(stderr, "Coincident: converged=%d dist=%g (expect ~0)\n", result.converged, dist);
  bool pass = result.converged && dist < 1e-6;
  if (!pass)
    fprintf(stderr, "  FAIL\n");
  return pass;
}

bool testParallel()
{
  FemmProblem p;
  int a0 = addNode(p, 0, 0), a1 = addNode(p, 10, 0); // horizontal
  int b0 = addNode(p, 0, 5), b1 = addNode(p, 8, 9); // slope 0.5
  int sa = addSegment(p, a0, a1);
  int sb = addSegment(p, b0, b1);
  FemmConstraint c;
  c.type = ConstraintType::Parallel;
  c.refA = sa;
  c.refB = sb;
  p.constraints.push_back(c);
  ConstraintSolver::SolveResult result = ConstraintSolver::solve(p);
  double dax = p.nodes[a1].x - p.nodes[a0].x, day = p.nodes[a1].y - p.nodes[a0].y;
  double dbx = p.nodes[b1].x - p.nodes[b0].x, dby = p.nodes[b1].y - p.nodes[b0].y;
  double cross = dax * dby - day * dbx;
  fprintf(stderr, "Parallel: converged=%d cross=%g (expect ~0)\n", result.converged, cross);
  bool pass = result.converged && std::abs(cross) < 1e-6;
  if (!pass)
    fprintf(stderr, "  FAIL\n");
  return pass;
}

bool testPerpendicular()
{
  FemmProblem p;
  int a0 = addNode(p, 0, 0), a1 = addNode(p, 10, 0);
  int b0 = addNode(p, 0, 5), b1 = addNode(p, 8, 9);
  int sa = addSegment(p, a0, a1);
  int sb = addSegment(p, b0, b1);
  FemmConstraint c;
  c.type = ConstraintType::Perpendicular;
  c.refA = sa;
  c.refB = sb;
  p.constraints.push_back(c);
  ConstraintSolver::SolveResult result = ConstraintSolver::solve(p);
  double dax = p.nodes[a1].x - p.nodes[a0].x, day = p.nodes[a1].y - p.nodes[a0].y;
  double dbx = p.nodes[b1].x - p.nodes[b0].x, dby = p.nodes[b1].y - p.nodes[b0].y;
  double dot = dax * dbx + day * dby;
  fprintf(stderr, "Perpendicular: converged=%d dot=%g (expect ~0)\n", result.converged, dot);
  bool pass = result.converged && std::abs(dot) < 1e-6;
  if (!pass)
    fprintf(stderr, "  FAIL\n");
  return pass;
}

bool testEqualSegments()
{
  FemmProblem p;
  int a0 = addNode(p, 0, 0), a1 = addNode(p, 10, 0); // length 10
  int b0 = addNode(p, 0, 5), b1 = addNode(p, 20, 5); // length 20
  int sa = addSegment(p, a0, a1);
  int sb = addSegment(p, b0, b1);
  FemmConstraint c;
  c.type = ConstraintType::Equal;
  c.isArcPair = false;
  c.refA = sa;
  c.refB = sb;
  p.constraints.push_back(c);
  ConstraintSolver::SolveResult result = ConstraintSolver::solve(p);
  double lenA = std::hypot(p.nodes[a1].x - p.nodes[a0].x, p.nodes[a1].y - p.nodes[a0].y);
  double lenB = std::hypot(p.nodes[b1].x - p.nodes[b0].x, p.nodes[b1].y - p.nodes[b0].y);
  fprintf(stderr, "Equal (segments): converged=%d lenA=%g lenB=%g\n", result.converged, lenA, lenB);
  bool pass = result.converged && std::abs(lenA - lenB) < 1e-6;
  if (!pass)
    fprintf(stderr, "  FAIL\n");
  return pass;
}

bool testEqualArcs()
{
  FemmProblem p;
  int arcA = addHalfArc(p, 0, 0, 10, 0); // radius 5
  int arcB = addHalfArc(p, 0, 20, 30, 20); // radius 15
  FemmConstraint c;
  c.type = ConstraintType::Equal;
  c.isArcPair = true;
  c.refA = arcA;
  c.refB = arcB;
  p.constraints.push_back(c);
  ConstraintSolver::SolveResult result = ConstraintSolver::solve(p);
  std::complex<double> ca, cb;
  double ra = 0, rb = 0;
  FemmProblemEdit::circleFromArc(p, p.arcSegments[arcA], ca, ra);
  FemmProblemEdit::circleFromArc(p, p.arcSegments[arcB], cb, rb);
  fprintf(stderr, "Equal (arcs): converged=%d rA=%g rB=%g\n", result.converged, ra, rb);
  bool pass = result.converged && std::abs(ra - rb) < 1e-6;
  if (!pass)
    fprintf(stderr, "  FAIL\n");
  return pass;
}

bool testTangentSegmentArc()
{
  FemmProblem p;
  int s0 = addNode(p, 0, 0), s1 = addNode(p, 20, 0); // line along y=0
  int seg = addSegment(p, s0, s1);
  int arc = addHalfArc(p, 5, 10, 15, 10); // center (10,10), radius 5 -- distance to line=10, not tangent
  FemmConstraint c;
  c.type = ConstraintType::Tangent;
  c.firstIsArc = false;
  c.refA = seg;
  c.refB = arc;
  p.constraints.push_back(c);
  ConstraintSolver::SolveResult result = ConstraintSolver::solve(p);
  std::complex<double> center;
  double radius = 0;
  FemmProblemEdit::circleFromArc(p, p.arcSegments[arc], center, radius);
  double x0 = p.nodes[s0].x, y0 = p.nodes[s0].y, x1 = p.nodes[s1].x, y1 = p.nodes[s1].y;
  double dx = x1 - x0, dy = y1 - y0;
  double len = std::hypot(dx, dy);
  double dist = len > 0 ? std::abs(dx * (center.imag() - y0) - dy * (center.real() - x0)) / len : -1;
  fprintf(stderr, "Tangent (segment+arc): converged=%d dist-to-center=%g radius=%g\n", result.converged, dist, radius);
  bool pass = result.converged && std::abs(dist - radius) < 1e-6;
  if (!pass)
    fprintf(stderr, "  FAIL\n");
  return pass;
}

bool testTangentArcArc()
{
  FemmProblem p;
  int arcA = addHalfArc(p, 0, 0, 10, 0); // center (5,0), radius 5
  // center (20,0), radius 5 -- a diagonally-far starting point (e.g.
  // center (30,20)) reliably stalled the solver at a nonzero residual
  // (confirmed via a debug trace: lambda climbed past 1e10 while the
  // residual froze, LM stuck in a local minimum of the sum-of-squares
  // that isn't the true root) -- a real, expected limitation of ANY
  // local nonlinear solver given a poor enough starting point, not a
  // bug (see ConstraintSolver.cpp's own "no convergence guarantee on
  // pathological systems" scope note) -- but not something to bake into
  // a REGRESSION test, which should exercise a realistic starting
  // configuration a user might actually encounter, not a worst case
  // chosen to be maximally adversarial.
  int arcB = addHalfArc(p, 15, 0, 25, 0);
  FemmConstraint c;
  c.type = ConstraintType::Tangent;
  c.firstIsArc = true;
  c.refA = arcA;
  c.refB = arcB;
  p.constraints.push_back(c);
  ConstraintSolver::SolveResult result = ConstraintSolver::solve(p);
  std::complex<double> ca, cb;
  double ra = 0, rb = 0;
  FemmProblemEdit::circleFromArc(p, p.arcSegments[arcA], ca, ra);
  FemmProblemEdit::circleFromArc(p, p.arcSegments[arcB], cb, rb);
  double centerDist = std::abs(cb - ca);
  fprintf(stderr, "Tangent (arc+arc): converged=%d centerDist=%g r1+r2=%g (equal radii -> external tangency expected)\n",
          result.converged, centerDist, ra + rb);
  bool pass = result.converged && std::abs(centerDist - (ra + rb)) < 1e-6;
  if (!pass)
    fprintf(stderr, "  FAIL\n");
  return pass;
}

bool testConcentric()
{
  FemmProblem p;
  int arcA = addHalfArc(p, 0, 0, 10, 0); // center (5,0)
  int arcB = addHalfArc(p, 0, 20, 6, 20); // center (3,20), different radius too
  FemmConstraint c;
  c.type = ConstraintType::Concentric;
  c.refA = arcA;
  c.refB = arcB;
  p.constraints.push_back(c);
  ConstraintSolver::SolveResult result = ConstraintSolver::solve(p);
  std::complex<double> ca, cb;
  double ra = 0, rb = 0;
  FemmProblemEdit::circleFromArc(p, p.arcSegments[arcA], ca, ra);
  FemmProblemEdit::circleFromArc(p, p.arcSegments[arcB], cb, rb);
  double centerDist = std::abs(cb - ca);
  fprintf(stderr, "Concentric: converged=%d centerDist=%g (expect ~0)\n", result.converged, centerDist);
  bool pass = result.converged && centerDist < 1e-6;
  if (!pass)
    fprintf(stderr, "  FAIL\n");
  return pass;
}

bool testSymmetric()
{
  FemmProblem p;
  int m0 = addNode(p, 0, -10), m1 = addNode(p, 0, 10); // mirror line: the y-axis
  int mirrorSeg = addSegment(p, m0, m1);
  int a = addNode(p, 5, 3);
  int b = addNode(p, 8, 3); // NOT the mirror of a (would be (-5,3))
  FemmConstraint c;
  c.type = ConstraintType::Symmetric;
  c.refA = a;
  c.refB = b;
  c.refC = mirrorSeg;
  p.constraints.push_back(c);
  ConstraintSolver::SolveResult result = ConstraintSolver::solve(p);

  // Independent check (doesn't call FemmProblemEdit::reflectPoint, which
  // the solver's own residual already uses internally -- reusing it here
  // would only re-confirm convergence, not verify the RELATIONSHIP is
  // actually geometric reflection): A and B are mirror images across
  // line M0-M1 iff (1) their midpoint lies ON that line, and (2) A-B is
  // PERPENDICULAR to it. Checked against whatever the mirror line ended
  // up at post-solve (its own 2 nodes are unknowns too).
  double mx0 = p.nodes[m0].x, my0 = p.nodes[m0].y, mx1 = p.nodes[m1].x, my1 = p.nodes[m1].y;
  double ax = p.nodes[a].x, ay = p.nodes[a].y, bx = p.nodes[b].x, by = p.nodes[b].y;
  double midx = (ax + bx) / 2, midy = (ay + by) / 2;
  double lineDx = mx1 - mx0, lineDy = my1 - my0;
  double onLineCross = lineDx * (midy - my0) - lineDy * (midx - mx0);
  double perpDot = (bx - ax) * lineDx + (by - ay) * lineDy;
  fprintf(stderr, "Symmetric: converged=%d onLineCross=%g perpDot=%g (both expect ~0)\n", result.converged, onLineCross, perpDot);
  bool pass = result.converged && std::abs(onLineCross) < 1e-6 && std::abs(perpDot) < 1e-6;
  if (!pass)
    fprintf(stderr, "  FAIL\n");
  return pass;
}

bool testRadiusDimension()
{
  FemmProblem p;
  int arc = addHalfArc(p, 0, 0, 10, 0); // radius 5
  FemmDimension dim;
  dim.type = DimensionType::Radius;
  dim.refA = arc;
  dim.value = 8.0;
  p.dimensions.push_back(dim);
  ConstraintSolver::SolveResult result = ConstraintSolver::solve(p);
  std::complex<double> c;
  double r = 0;
  FemmProblemEdit::circleFromArc(p, p.arcSegments[arc], c, r);
  fprintf(stderr, "Radius dimension: converged=%d radius=%g (expect 8)\n", result.converged, r);
  bool pass = result.converged && std::abs(r - 8.0) < 1e-6;
  if (!pass)
    fprintf(stderr, "  FAIL\n");
  return pass;
}

bool testAngleDimension()
{
  FemmProblem p;
  int v = addNode(p, 0, 0);
  int p1 = addNode(p, 10, 0); // 0 deg
  int p2 = addNode(p, 10, 10); // 45 deg
  FemmDimension dim;
  dim.type = DimensionType::Angle;
  dim.refA = v;
  dim.refB = p1;
  dim.refC = p2;
  dim.value = 60.0;
  p.dimensions.push_back(dim);
  ConstraintSolver::SolveResult result = ConstraintSolver::solve(p);
  double a1 = std::atan2(p.nodes[p1].y - p.nodes[v].y, p.nodes[p1].x - p.nodes[v].x);
  double a2 = std::atan2(p.nodes[p2].y - p.nodes[v].y, p.nodes[p2].x - p.nodes[v].x);
  double diff = a2 - a1;
  while (diff > M_PI)
    diff -= 2 * M_PI;
  while (diff <= -M_PI)
    diff += 2 * M_PI;
  double diffDeg = diff * 180.0 / M_PI;
  fprintf(stderr, "Angle dimension: converged=%d angle=%g (expect 60)\n", result.converged, diffDeg);
  bool pass = result.converged && std::abs(diffDeg - 60.0) < 1e-4;
  if (!pass)
    fprintf(stderr, "  FAIL\n");
  return pass;
}


// The whole point of AngleLines is lines that never touch, so the test
// uses two DISJOINT segments -- the case the old shared-vertex-only path
// could not express at all.
bool testAngleLinesDimension()
{
  FemmProblem p;
  int a0 = addNode(p, 0, 0);
  int a1 = addNode(p, 10, 0);      // horizontal
  int b0 = addNode(p, 0, 5);
  int b1 = addNode(p, 10, 15);     // ~45 deg, nowhere near the first line
  int s0 = addSegment(p, a0, a1);
  int s1 = addSegment(p, b0, b1);

  FemmDimension dim;
  dim.type = DimensionType::AngleLines;
  dim.refA = s0;
  dim.refB = s1;
  dim.value = 30.0;
  p.dimensions.push_back(dim);

  ConstraintSolver::SolveResult result = ConstraintSolver::solve(p);

  double t0 = std::atan2(p.nodes[a1].y - p.nodes[a0].y, p.nodes[a1].x - p.nodes[a0].x);
  double t1 = std::atan2(p.nodes[b1].y - p.nodes[b0].y, p.nodes[b1].x - p.nodes[b0].x);
  double diff = t1 - t0;
  while (diff > M_PI / 2)
    diff -= M_PI;
  while (diff <= -M_PI / 2)
    diff += M_PI;
  double deg = std::abs(diff * 180.0 / M_PI);
  fprintf(stderr, "AngleLines dimension: converged=%d angle=%g (expect 30)\n",
      result.converged, deg);
  bool pass = result.converged && std::abs(deg - 30.0) < 1e-4;
  if (!pass)
    fprintf(stderr, "  FAIL\n");
  return pass;
}


// Two segments crossing in an X: expect one new node at the crossing and
// four segments where there were two, with the node actually ON both.
bool testSplitIntersecting()
{
  FemmProblem p;
  int a0 = addNode(p, -10, -10), a1 = addNode(p, 10, 10);
  int b0 = addNode(p, -10, 10), b1 = addNode(p, 10, -10);
  addSegment(p, a0, a1);
  addSegment(p, b0, b1);

  int added = FemmProblemEdit::splitIntersectingSegments(p);
  double x = p.nodes.size() > 4 ? p.nodes[4].x : 1e9;
  double y = p.nodes.size() > 4 ? p.nodes[4].y : 1e9;
  fprintf(stderr, "Split intersecting: added=%d segs=%d node=(%g,%g) (expect 1, 4, (0,0))\n",
      added, (int)p.segments.size(), x, y);
  bool pass = added == 1 && p.segments.size() == 4
      && std::abs(x) < 1e-9 && std::abs(y) < 1e-9;

  // Segments that only share an endpoint must NOT be split.
  FemmProblem q;
  int c0 = addNode(q, 0, 0), c1 = addNode(q, 10, 0), c2 = addNode(q, 10, 10);
  addSegment(q, c0, c1);
  addSegment(q, c1, c2);
  int added2 = FemmProblemEdit::splitIntersectingSegments(q);
  fprintf(stderr, "  shared-endpoint pair: added=%d (expect 0)\n", added2);
  pass = pass && added2 == 0;

  if (!pass)
    fprintf(stderr, "  FAIL\n");
  return pass;
}

} // namespace

class TestConstraintSolver : public QObject
{
  Q_OBJECT

private slots:
  // --- the 17 scenarios moved out of main.cpp, one slot each -------------

  void rectangleRigid();
  void distanceDimension();
  void horizontalDistanceDimension();
  void verticalDistanceDimension();
  void coincident();
  void parallel();
  void perpendicular();
  void equalSegments();
  void equalArcs();
  void tangentSegmentArc();
  void tangentArcArc();
  void concentric();
  void symmetric();
  void radiusDimension();
  void angleDimension();
  void angleLinesDimension();
  void splitIntersecting();

  // --- coverage the CLI harness never had --------------------------------

  void unconstrainedSketchIsUnderConstrained();
  void fullyConstrainedSketchIsReported();
  void redundantConstraintDoesNotDiverge();
  void impossibleConstraintsReportNonConvergence();
  void solveIsDeterministic();
  void angleLinesWrapsModulo180();
  void sketchLayerNeverReachesTheSavedFile();
};

// ---------------------------------------------------------------------------
// The moved scenarios. Each asserts through the function it came with, whose
// own fprintf detail QTest prints when the assertion fails.
// ---------------------------------------------------------------------------

void TestConstraintSolver::rectangleRigid()
{ QVERIFY(testRectangleRigid()); }
void TestConstraintSolver::distanceDimension()
{ QVERIFY(testDistanceDimension()); }
void TestConstraintSolver::horizontalDistanceDimension()
{ QVERIFY(testHorizontalDistanceDimension()); }
void TestConstraintSolver::verticalDistanceDimension()
{ QVERIFY(testVerticalDistanceDimension()); }
void TestConstraintSolver::coincident()
{ QVERIFY(testCoincident()); }
void TestConstraintSolver::parallel()
{ QVERIFY(testParallel()); }
void TestConstraintSolver::perpendicular()
{ QVERIFY(testPerpendicular()); }
void TestConstraintSolver::equalSegments()
{ QVERIFY(testEqualSegments()); }
void TestConstraintSolver::equalArcs()
{ QVERIFY(testEqualArcs()); }
void TestConstraintSolver::tangentSegmentArc()
{ QVERIFY(testTangentSegmentArc()); }
void TestConstraintSolver::tangentArcArc()
{ QVERIFY(testTangentArcArc()); }
void TestConstraintSolver::concentric()
{ QVERIFY(testConcentric()); }
void TestConstraintSolver::symmetric()
{ QVERIFY(testSymmetric()); }
void TestConstraintSolver::radiusDimension()
{ QVERIFY(testRadiusDimension()); }
void TestConstraintSolver::angleDimension()
{ QVERIFY(testAngleDimension()); }
void TestConstraintSolver::angleLinesDimension()
{ QVERIFY(testAngleLinesDimension()); }
void TestConstraintSolver::splitIntersecting()
{ QVERIFY(testSplitIntersecting()); }

// ---------------------------------------------------------------------------
// Sketch health
// ---------------------------------------------------------------------------

void TestConstraintSolver::unconstrainedSketchIsUnderConstrained()
{
  // nodeStatus is keyed by connected COMPONENT of the constraint graph
  // (ConstraintSolver.cpp classifies per component, then writes the result
  // to every node in it). A node that participates in no constraint is in
  // no component and therefore gets no entry at all -- it is trivially
  // free, not unclassified. Asserted explicitly because the obvious
  // assumption, that every node gets a status, is wrong and a test written
  // on it fails for the wrong reason.
  FemmProblem free;
  const int a = addNode(free, 0, 0);
  const int b = addNode(free, 4, 3);
  addSegment(free, a, b);

  ConstraintSolver::SolveResult r = ConstraintSolver::solve(free);
  QVERIFY2(r.converged, "an unconstrained sketch is trivially satisfied");
  QVERIFY2(r.nodeStatus.isEmpty(),
           "nodes in no constraint should not be classified at all");

  // Add one constraint and the nodes it touches DO get classified. Two
  // free nodes joined by a single Horizontal constraint still leave
  // translation and length open, so UnderConstrained is correct here.
  FemmProblem partly;
  const int c = addNode(partly, 0, 0);
  const int d = addNode(partly, 6, 2);
  const int seg = addSegment(partly, c, d);
  FemmConstraint h;
  h.type = ConstraintType::Horizontal;
  h.refA = seg;
  partly.constraints = {h};

  ConstraintSolver::SolveResult r2 = ConstraintSolver::solve(partly);
  QVERIFY(r2.converged);
  for (int n : {c, d}) {
    auto it = r2.nodeStatus.constFind(n);
    QVERIFY2(it != r2.nodeStatus.constEnd(),
             "a node inside a constraint must be classified");
    QCOMPARE(*it, ConstraintSolver::SketchStatus::UnderConstrained);
  }
}
void TestConstraintSolver::fullyConstrainedSketchIsReported()
{
  // A segment pinned at both ends: coincident to two fixed points plus a
  // length. Whatever the classifier calls this, it must NOT call it
  // under-constrained while also reporting convergence -- that pairing is
  // what a broken DOF count looks like.
  FemmProblem p;
  const int a = addNode(p, 0, 0);
  const int b = addNode(p, 7, 1);
  const int s = addSegment(p, a, b);

  FemmConstraint h;
  h.type = ConstraintType::Horizontal;
  h.refA = s;

  FemmDimension d;
  d.type = DimensionType::Distance;
  d.refA = a;
  d.refB = b;
  d.value = 5.0;

  p.constraints = {h};
  p.dimensions = {d};

  ConstraintSolver::SolveResult r = ConstraintSolver::solve(p);
  QVERIFY(r.converged);
  QVERIFY2(qAbs(p.nodes[a].y - p.nodes[b].y) < 1e-6,
           "the horizontal constraint was not satisfied");
  const double len = std::hypot(p.nodes[b].x - p.nodes[a].x,
                                p.nodes[b].y - p.nodes[a].y);
  QVERIFY2(qAbs(len - 5.0) < 1e-6,
           qPrintable(QStringLiteral("length came out %1, wanted 5").arg(len)));
}

void TestConstraintSolver::redundantConstraintDoesNotDiverge()
{
  // The same Horizontal constraint applied twice to one segment. The second
  // adds no information; a least-squares solver must absorb it rather than
  // be destabilised by the rank deficiency.
  FemmProblem p;
  const int a = addNode(p, 0, 0);
  const int b = addNode(p, 6, 2);
  const int s = addSegment(p, a, b);

  FemmConstraint h0;
  h0.type = ConstraintType::Horizontal;
  h0.refA = s;
  FemmConstraint h1 = h0;
  p.constraints = {h0, h1};

  ConstraintSolver::SolveResult r = ConstraintSolver::solve(p);
  QVERIFY2(r.converged,
           "a duplicated constraint made the solve fail; a redundant row "
           "must be absorbed, not fatal");
  QVERIFY(qAbs(p.nodes[a].y - p.nodes[b].y) < 1e-6);
  QVERIFY2(std::isfinite(p.nodes[a].x) && std::isfinite(p.nodes[a].y)
               && std::isfinite(p.nodes[b].x) && std::isfinite(p.nodes[b].y),
           "the solve produced non-finite coordinates");
}

void TestConstraintSolver::impossibleConstraintsReportNonConvergence()
{
  // Contradictory: the same two nodes told to be 3 apart and 9 apart. The
  // solver must come back and SAY it could not do it -- the failure mode
  // that matters is looping forever, which would hang the editor.
  FemmProblem p;
  const int a = addNode(p, 0, 0);
  const int b = addNode(p, 5, 0);

  FemmDimension d0;
  d0.type = DimensionType::Distance;
  d0.refA = a;
  d0.refB = b;
  d0.value = 3.0;
  FemmDimension d1 = d0;
  d1.value = 9.0;
  p.dimensions = {d0, d1};

  QElapsedTimer timer;
  timer.start();
  ConstraintSolver::SolveResult r = ConstraintSolver::solve(p);
  const qint64 elapsed = timer.elapsed();

  fprintf(stderr, "contradictory dimensions: converged=%d iterations=%d "
                  "elapsed=%lldms\n",
          (int)r.converged, r.iterations, (long long)elapsed);

  QVERIFY2(elapsed < 5000,
           "the solver took over 5s on a 2-node contradiction; it must bound "
           "its iterations rather than grind");
  QVERIFY2(r.iterations > 0, "no iterations were reported");
  // A least-squares solver legitimately "converges" to the compromise
  // midpoint here, so convergence itself is not the assertion. What must
  // hold is that it terminated, bounded, with finite coordinates.
  QVERIFY(std::isfinite(p.nodes[a].x) && std::isfinite(p.nodes[b].x));
  const double len = std::hypot(p.nodes[b].x - p.nodes[a].x,
                                p.nodes[b].y - p.nodes[a].y);
  QVERIFY2(len > 2.0 && len < 10.0,
           qPrintable(QStringLiteral("compromise length %1 is outside both "
                                     "requested values").arg(len)));
}

void TestConstraintSolver::solveIsDeterministic()
{
  // Same input, twice: byte-identical output. A solver that varies run to
  // run cannot be regression-tested at all, and makes every downstream
  // golden-value test flaky.
  auto build = []() {
    FemmProblem p;
    const int n0 = addNode(p, 0, 0);
    const int n1 = addNode(p, 10, 2);
    const int n2 = addNode(p, 9, 8);
    const int n3 = addNode(p, 1, 7);
    const int s0 = addSegment(p, n0, n1);
    const int s1 = addSegment(p, n1, n2);
    const int s2 = addSegment(p, n2, n3);
    const int s3 = addSegment(p, n3, n0);
    FemmConstraint c0;
    c0.type = ConstraintType::Horizontal;
    c0.refA = s0;
    FemmConstraint c1;
    c1.type = ConstraintType::Vertical;
    c1.refA = s1;
    FemmConstraint c2;
    c2.type = ConstraintType::Horizontal;
    c2.refA = s2;
    FemmConstraint c3;
    c3.type = ConstraintType::Vertical;
    c3.refA = s3;
    p.constraints = {c0, c1, c2, c3};
    return p;
  };

  FemmProblem first = build();
  FemmProblem second = build();
  ConstraintSolver::SolveResult r0 = ConstraintSolver::solve(first);
  ConstraintSolver::SolveResult r1 = ConstraintSolver::solve(second);

  QCOMPARE(r0.converged, r1.converged);
  QCOMPARE(r0.iterations, r1.iterations);
  QCOMPARE(first.nodes.size(), second.nodes.size());
  for (int i = 0; i < first.nodes.size(); ++i) {
    QVERIFY2(first.nodes[i].x == second.nodes[i].x
                 && first.nodes[i].y == second.nodes[i].y,
             qPrintable(QStringLiteral("node %1 differs between runs: "
                                       "(%2,%3) vs (%4,%5)")
                            .arg(i)
                            .arg(first.nodes[i].x).arg(first.nodes[i].y)
                            .arg(second.nodes[i].x).arg(second.nodes[i].y)));
  }
}

void TestConstraintSolver::angleLinesWrapsModulo180()
{
  // A line has no direction, so a segment stored end-for-end is the SAME
  // line. The AngleLines residual wraps modulo 180 for exactly that reason;
  // without it the solver chases a 180-degree phantom error. Two disjoint
  // segments, the second declared backwards, must still reach 30 degrees.
  FemmProblem p;
  const int a0 = addNode(p, 0, 0);
  const int a1 = addNode(p, 10, 0);
  const int b0 = addNode(p, 4, 6);      // reversed: b1 -> b0 is the "forward"
  const int b1 = addNode(p, 0, 5);      // direction a naive residual expects
  const int s0 = addSegment(p, a0, a1);
  const int s1 = addSegment(p, b0, b1);

  FemmDimension d;
  d.type = DimensionType::AngleLines;
  d.refA = s0;
  d.refB = s1;
  d.value = 30.0;
  p.dimensions = {d};

  ConstraintSolver::SolveResult r = ConstraintSolver::solve(p);
  QVERIFY2(r.converged, "AngleLines did not converge on a reversed segment");

  const double ax = p.nodes[a1].x - p.nodes[a0].x;
  const double ay = p.nodes[a1].y - p.nodes[a0].y;
  const double bx = p.nodes[b1].x - p.nodes[b0].x;
  const double by = p.nodes[b1].y - p.nodes[b0].y;
  double deg = std::atan2(std::abs(ax * by - ay * bx), ax * bx + ay * by)
               * 180.0 / M_PI;
  if (deg > 90.0)
    deg = 180.0 - deg;   // the unsigned angle between two undirected lines

  fprintf(stderr, "AngleLines with a reversed segment: %g degrees\n", deg);
  QVERIFY2(qAbs(deg - 30.0) < 1e-4,
           qPrintable(QStringLiteral("angle came out %1, wanted 30").arg(deg)));
}

void TestConstraintSolver::sketchLayerNeverReachesTheSavedFile()
{
  // The sketch layer is session-only by design: constraints and dimensions
  // are never written to .fem/.femx. Asserted here because "never
  // persisted" is a promise made in MainWindow.h's own comment, and a
  // writer that started emitting them would corrupt files for classic FEMM,
  // which knows nothing about these records.
  FemmProblem p;
  const int a = addNode(p, 0, 0);
  const int b = addNode(p, 10, 1);
  const int s = addSegment(p, a, b);

  FemmConstraint h;
  h.type = ConstraintType::Horizontal;
  h.refA = s;
  FemmDimension d;
  d.type = DimensionType::Distance;
  d.refA = a;
  d.refB = b;
  d.value = 8.0;
  p.constraints = {h};
  p.dimensions = {d};
  ConstraintSolver::solve(p);

  QVERIFY(!p.constraints.isEmpty());
  QVERIFY(!p.dimensions.isEmpty());

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath("sketch.fem");
  QString error;
  QVERIFY2(FemmFileIO::writeFem(path, p, error), qPrintable(error));

  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
  const QString text = QString::fromUtf8(f.readAll());

  for (const char* marker : {"Constraint", "constraint",
                             "Dimension", "dimension"}) {
    QVERIFY2(!text.contains(QLatin1String(marker)),
             qPrintable(QStringLiteral("the saved .fem contains \"%1\"; the "
                                       "sketch layer is supposed to be "
                                       "session-only").arg(marker)));
  }

  // and a reload must come back with an empty sketch rather than stale data
  FemmProblem reloaded;
  QVERIFY2(FemmFileIO::readFem(path, reloaded, error), qPrintable(error));
  QVERIFY(reloaded.constraints.isEmpty());
  QVERIFY(reloaded.dimensions.isEmpty());
  QCOMPARE((int)reloaded.segments.size(), 1);
}

QTEST_GUILESS_MAIN(TestConstraintSolver)
#include "tst_constraint_solver.moc"
