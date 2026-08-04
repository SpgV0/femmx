#pragma once

// Modified by Claude (Anthropic), noreply@anthropic.com: per direct user
// report ("the density plot for H is wrong in FEMM QT, compare with the
// old gui") -- MeshSolutionElement::muX/muY are a LINEAR-material-only
// placeholder (see that field's own comment); for a nonlinear (BH-curve)
// material like a real transformer/choke core, femmqt's H density plot
// was computing H = B/(1*mu0) using that placeholder mu_x=1, off from
// the correct value by a factor of the material's real (B-dependent)
// permeability -- confirmed live via mo_getpointvalues on a real
// nanocrystalline-core model: classic reported mu_r ~8.2 million at a
// low-flux point (a real, physically correct value near a steep-BH-
// curve material's initial slope) where femmqt's old formula implicitly
// used mu_r=1, an error of ~5-6 orders of magnitude, not a rounding
// difference.
//
// This ports femm/Problem.cpp's CMaterialProp::GetSlopes/GetH/GetMu --
// the natural-cubic-Hermite-spline BH-curve interpolation classic uses
// -- for the real-valued (DC/magnetostatic, Frequency==0), unlaminated
// (LamType==0) case specifically, which is what every nonlinear-material
// test model built this session actually uses. NOT ported (left as a
// documented gap, matching this codebase's existing convention of
// noting deferred cases rather than silently approximating them):
//   - AC/harmonic nonlinear materials (Frequency != 0) -- classic first
//     "doctors" the BH curve into a frequency-dependent effective curve
//     (convolving with a sinusoid, then a complex-permeability hysteresis
//     kludge) before ever computing slopes; a materially different,
//     complex-valued algorithm, not just this one with complex numbers
//     substituted in.
//   - Laminated nonlinear materials (LamType 1 or 2) -- classic solves a
//     1-D finite-element problem across the lamination for AC, and even
//     the simpler DC LamType 1/2 formulas (see GetMu) aren't ported here.
//   - Permanent-magnet nonlinear materials (H_c != 0) -- GetH's coercive-
//     force shift (CFemmviewDoc::GetH's "d_ShiftH" branch) isn't applied.
// Elements using any of these fall back to the existing muX/muY-based
// formula, same as before this fix -- still wrong for them, but no
// worse than before, and clearly scoped rather than silently guessed.

#include <QVector>

#include <cmath>

namespace BHCurve {

// One material's BH curve, ready for interpolation -- computeSlopes()
// below fills `slope` (and may adjust `b`/`h` in place, see its own
// comment) from the raw (B, H) points a .fem/.ans file provides.
struct Curve {
  QVector<double> b;
  QVector<double> h;
  QVector<double> slope;
};

// Solves femm/Problem.cpp's CMaterialProp::GetSlopes' tridiagonal
// natural-cubic-spline system (real-valued/DC case only) via the Thomas
// algorithm -- mathematically identical to classic's general Gaussian
// elimination for this tridiagonal matrix, just O(n) instead of O(n^3).
// Boundary rows (natural BC) and interior rows match GetSlopes' L.M/L.b
// setup exactly (femm/Problem.cpp:236-266).
inline QVector<double> solveTridiagonalSlopes(const QVector<double>& b, const QVector<double>& h)
{
  int n = b.size();
  QVector<double> diag(n), sub(n), sup(n), rhs(n);

  double l1 = b[1] - b[0];
  diag[0] = 4.0 / l1;
  sup[0] = 2.0 / l1;
  rhs[0] = 6.0 * (h[1] - h[0]) / (l1 * l1);

  double lLast = b[n - 1] - b[n - 2];
  sub[n - 1] = 2.0 / lLast;
  diag[n - 1] = 4.0 / lLast;
  rhs[n - 1] = 6.0 * (h[n - 1] - h[n - 2]) / (lLast * lLast);

  for (int i = 1; i < n - 1; i++) {
    double la = b[i] - b[i - 1];
    double lb = b[i + 1] - b[i];
    sub[i] = 2.0 / la;
    diag[i] = 4.0 * (la + lb) / (la * lb);
    sup[i] = 2.0 / lb;
    rhs[i] = 6.0 * (h[i] - h[i - 1]) / (la * la) + 6.0 * (h[i + 1] - h[i]) / (lb * lb);
  }

  // Thomas algorithm forward sweep.
  QVector<double> cPrime(n), dPrime(n);
  cPrime[0] = sup[0] / diag[0];
  dPrime[0] = rhs[0] / diag[0];
  for (int i = 1; i < n; i++) {
    double m = diag[i] - sub[i] * cPrime[i - 1];
    if (i < n - 1)
      cPrime[i] = sup[i] / m;
    dPrime[i] = (rhs[i] - sub[i] * dPrime[i - 1]) / m;
  }

  QVector<double> slope(n);
  slope[n - 1] = dPrime[n - 1];
  for (int i = n - 2; i >= 0; i--)
    slope[i] = dPrime[i] - cPrime[i] * slope[i + 1];
  return slope;
}

// Matches GetSlopes' post-solve "is this curve segment monotonic"
// check (femm/Problem.cpp:271-304): for each segment, the cubic
// Hermite piece is written in the local form c0 + c1*x + c2*x^2 and
// checked for a root of its DERIVATIVE inside (0, L) -- a root there
// means the interpolated H has a local min/max inside the segment
// (non-physical for a BH curve), so the segment needs smoothing.
inline bool curveIsMonotonic(const QVector<double>& b, const QVector<double>& h, const QVector<double>& slope)
{
  for (int i = 1; i < b.size(); i++) {
    double d0 = slope[i - 1], d1 = slope[i];
    double u0 = h[i - 1], u1 = h[i];
    double L = b[i] - b[i - 1];
    double c0 = d0;
    double c1 = -(2.0 * (2.0 * d0 * L + d1 * L + 3.0 * u0 - 3.0 * u1)) / (L * L);
    double c2 = (3.0 * (d0 * L + d1 * L + 2.0 * u0 - 2.0 * u1)) / (L * L * L);
    double x0 = -1.0, x1 = -1.0;
    double disc = c1 * c1 - 4.0 * c0 * c2;
    if (c2 == 0.0) {
      if (c1 != 0.0)
        x0 = -c0 / c1;
    } else if (disc > 0.0) {
      double sq = std::sqrt(disc);
      x0 = -(c1 + sq) / (2.0 * c2);
      x1 = (-c1 + sq) / (2.0 * c2);
    }
    if ((x0 >= 0.0 && x0 <= L) || (x1 >= 0.0 && x1 <= L))
      return false;
  }
  return true;
}

// Matches GetSlopes' remedial 3-point moving-average smoothing of the
// interior points (femm/Problem.cpp:306-320) -- endpoints untouched.
inline void smoothInteriorPoints(QVector<double>& b, QVector<double>& h)
{
  QVector<double> bn = b, hn = h;
  for (int i = 1; i < b.size() - 1; i++) {
    bn[i] = (b[i - 1] + b[i] + b[i + 1]) / 3.0;
    hn[i] = (h[i - 1] + h[i] + h[i + 1]) / 3.0;
  }
  b = bn;
  h = hn;
}

// Computes a Curve's slopes, iteratively smoothing the input points
// (mutating curve.b/curve.h, matching classic exactly) until the
// resulting spline is monotonic on every segment -- matches GetSlopes'
// own while(!CurveOK) loop. Real curves converge in 0-2 iterations in
// practice; applies a generous but finite cap rather than classic's
// unbounded loop, since this runs on a GUI thread.
inline void computeSlopes(Curve& curve)
{
  if (curve.b.size() < 2)
    return;
  constexpr int kMaxSmoothingIterations = 50;
  for (int iter = 0; iter < kMaxSmoothingIterations; iter++) {
    curve.slope = solveTridiagonalSlopes(curve.b, curve.h);
    if (curveIsMonotonic(curve.b, curve.h, curve.slope))
      return;
    smoothInteriorPoints(curve.b, curve.h);
  }
  // Give up smoothing after kMaxSmoothingIterations -- use whatever the
  // last solve produced rather than looping forever; a curve needing
  // this many passes is pathological input, not a case worth blocking
  // the UI over.
  curve.slope = solveTridiagonalSlopes(curve.b, curve.h);
}

// Matches CMaterialProp::GetH(double) (femm/Problem.cpp:502-531) --
// cubic Hermite interpolation of |H| at flux density magnitude `bMag`,
// using a Curve already processed by computeSlopes(). Returns 0 for
// bMag <= 0 (matching classic's b==0 case, since GetH takes a signed
// scalar there but every caller in this file passes a magnitude).
inline double interpolateH(const Curve& curve, double bMag)
{
  int n = curve.b.size();
  if (n == 0 || bMag <= 0.0)
    return 0.0;

  if (bMag > curve.b[n - 1])
    return curve.h[n - 1] + curve.slope[n - 1] * (bMag - curve.b[n - 1]);

  for (int i = 0; i < n - 1; i++) {
    if (bMag >= curve.b[i] && bMag <= curve.b[i + 1]) {
      double l = curve.b[i + 1] - curve.b[i];
      double z = (bMag - curve.b[i]) / l;
      double z2 = z * z;
      return (1.0 - 3.0 * z2 + 2.0 * z2 * z) * curve.h[i]
          + z * (1.0 - 2.0 * z + z2) * l * curve.slope[i]
          + z2 * (3.0 - 2.0 * z) * curve.h[i + 1]
          + z2 * (z - 1.0) * l * curve.slope[i + 1];
    }
  }
  return 0.0;
}

} // namespace BHCurve
