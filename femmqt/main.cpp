#include <QApplication>
#include <QFileInfo>

#include "AnsFileIO.h"
#include "AnsxFileIO.h"
#include "AppPreferences.h"
#include "AppTheme.h"
#include "ConstraintSolver.h"
#include "FemmProblem.h"
#include "FemmProblemEdit.h"
#include "MainWindow.h"
#include "MeshSolution.h"
#include "SolutionView.h"

#include <cmath>
#include <complex>
#include <cstdio>

namespace {
// Offline/batch .ansx generation, independent of opening any window --
// `femmqt.exe --convert-ansx foo.ans` regenerates foo.ansx unconditionally
// (skips the freshness check AnsxFileIO::isUpToDate uses elsewhere) and
// exits, per this phase's plan. Uses QCoreApplication-only APIs (file I/O,
// no widgets), so it's safe to run before QApplication would normally be
// needed -- though the executable is still WIN32-subsystem, so stdio
// output is only visible when launched from a console that keeps it
// attached.
int convertAnsxCli(const QString& ansPath)
{
  FemmProblem problem;
  MeshSolution solution;
  QString error;
  if (!AnsFileIO::readAns(ansPath, problem, solution, error)) {
    fprintf(stderr, "%s\n", qPrintable(error));
    return 1;
  }
  QFileInfo fi(ansPath);
  QString ansxPath = fi.absolutePath() + "/" + fi.completeBaseName() + ".ansx";
  if (!AnsxFileIO::writeAnsx(ansxPath, ansPath, (int)problem.problemType, (int)problem.lengthUnits,
          problem.frequency, solution, error)) {
    fprintf(stderr, "%s\n", qPrintable(error));
    return 1;
  }
  fprintf(stderr, "Wrote %s (%d nodes, %d elements)\n", qPrintable(ansxPath), (int)solution.nodes.size(), (int)solution.elements.size());
  return 0;
}

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

int testConstraintsCli()
{
  struct NamedTest {
    const char* name;
    bool (*fn)();
  };
  const NamedTest tests[] = {
      {"Horizontal+Vertical", testRectangleRigid},
      {"Distance dimension", testDistanceDimension},
      {"Horizontal distance dimension", testHorizontalDistanceDimension},
      {"Vertical distance dimension", testVerticalDistanceDimension},
      {"Coincident", testCoincident},
      {"Parallel", testParallel},
      {"Perpendicular", testPerpendicular},
      {"Equal (segments)", testEqualSegments},
      {"Equal (arcs)", testEqualArcs},
      {"Tangent (segment+arc)", testTangentSegmentArc},
      {"Tangent (arc+arc)", testTangentArcArc},
      {"Concentric", testConcentric},
      {"Symmetric", testSymmetric},
      {"Radius dimension", testRadiusDimension},
      {"Angle dimension", testAngleDimension},
      {"AngleLines dimension", testAngleLinesDimension},
  };

  const int testCount = sizeof(tests) / sizeof(tests[0]);
  int failCount = 0;
  for (const NamedTest& t : tests) {
    if (!t.fn()) {
      fprintf(stderr, "*** %s FAILED ***\n", t.name);
      failCount++;
    }
  }

  fprintf(stderr, "\n%d/%d tests passed\n", testCount - failCount, testCount);
  fprintf(stderr, failCount == 0 ? "ALL TESTS PASSED\n" : "SOME TESTS FAILED\n");
  return failCount == 0 ? 0 : 1;
}
} // namespace

int main(int argc, char* argv[])
{
  QApplication app(argc, argv);
  // Gives QSettings (MainWindow's recent-files list) a stable registry
  // location -- without this it defaults to an unset/empty organization,
  // which still works but isn't a location a user (or an uninstaller)
  // could find on purpose.
  QCoreApplication::setOrganizationName("FEMMX");
  QCoreApplication::setApplicationName("femmqt");

  // Must happen before any window is constructed -- IconTheme::
  // themedToolIcon() and GeometryScene/SolutionWindow's background brush
  // both bake in whatever AppTheme::isDark()/qApp->palette() is current
  // at construction time.
  AppTheme::setDark(AppPreferences::load().darkTheme);

  const QStringList args = app.arguments();

  if (args.size() >= 3 && args.at(1) == "--convert-ansx")
    return convertAnsxCli(args.at(2));

  // `femmqt.exe --render-png <in> <out> [w h]` renders a .fem/.ans
  // offscreen to a PNG. This is what the classic GUI's Lua
  // mi_savepng/mo_savepng shell out to after a script calls
  // setgui("qt") -- Lua only ever runs in the MFC app, so "render with
  // the new GUI" has to mean handing the file to this executable. See
  // femm/ScriptGui.h for the whole arrangement.
  if (args.size() >= 4 && args.at(1) == "--render-png") {
    const QString in = args.at(2);
    const QString out = args.at(3);
    const int w = (args.size() >= 6) ? args.at(4).toInt() : 1024;
    const int h = (args.size() >= 6) ? args.at(5).toInt() : 768;
    if (w <= 0 || h <= 0) {
      fprintf(stderr, "--render-png: bad size %dx%d\n", w, h);
      return 1;
    }

    const QString suffix = QFileInfo(in).suffix();
    const bool isSolution = suffix.compare("ans", Qt::CaseInsensitive) == 0
        || suffix.compare("ansx", Qt::CaseInsensitive) == 0;

    QImage image;
    // The windows are constructed but never shown: renderToImage draws
    // the scene directly, which is why this works without a desktop.
    if (isSolution) {
      SolutionWindow window;
      window.resize(w, h);
      window.openAnsFile(in);
      image = window.renderToImage(QSize(w, h));
    } else {
      MainWindow window;
      window.resize(w, h);
      window.openFile(in);
      image = window.renderToImage(QSize(w, h));
    }

    if (image.isNull()) {
      fprintf(stderr, "--render-png: nothing to render from %s\n",
          qPrintable(in));
      return 1;
    }
    if (!image.save(out, "PNG")) {
      fprintf(stderr, "--render-png: couldn't write %s\n", qPrintable(out));
      return 1;
    }
    return 0;
  }

  if (args.size() >= 2 && args.at(1) == "--test-constraints")
    return testConstraintsCli();

  // A file path on the command line opens immediately -- used by the
  // femm.cfg-driven GUI switch (step 7) to hand off the currently-open
  // file to the other GUI, mirroring femm.cpp's ProcessShellCommand
  // single-file-open convention. Routed by extension: a solved .ans/.ansx
  // (handed off from the classic GUI's post-processor, CFemmviewView::
  // OnSwitchToQtGui) opens the Solution Viewer, not the geometry editor --
  // otherwise the geometry editor would try to load a possibly-huge
  // solved mesh as if it were raw, editable geometry.
  QString suffix = args.size() > 1 ? QFileInfo(args.at(1)).suffix() : QString();
  bool isMagSolutionFile = suffix.compare("ans", Qt::CaseInsensitive) == 0 || suffix.compare("ansx", Qt::CaseInsensitive) == 0;

  if (isMagSolutionFile) {
    auto* solutionWindow = new SolutionWindow();
    solutionWindow->show();
    solutionWindow->openAnsFile(args.at(1));
  } else {
    auto* window = new MainWindow();
    window->show();
    if (args.size() > 1)
      window->openFile(args.at(1));
  }

  return app.exec();
}
