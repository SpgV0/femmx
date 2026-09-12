// Before <cmath>, and before anything that might pull it in: MSVC only
// defines M_PI when this is set. Same convention as
// FemmProblemEdit.cpp's first line.
#define _USE_MATH_DEFINES

#include "SnapEngine.h"

#include "FemmProblem.h"
#include "FemmProblemEdit.h"

#include <cmath>
#include <complex>

namespace {

using Complex = std::complex<double>;

struct Candidate {
  SnapEngine::SnapType type = SnapEngine::SnapType::None;
  double x = 0, y = 0;
  int index = -1;
  int otherIndex = -1;
  double distance = 0;
};

bool enabled(unsigned flags, unsigned bit)
{
  return (flags & bit) != 0;
}

double dist(double ax, double ay, double bx, double by)
{
  return std::hypot(ax - bx, ay - by);
}

// Nearest point on the SEGMENT (clamped), not on its infinite line.
void nearestOnSegment(double px, double py, double x0, double y0,
    double x1, double y1, double& nx, double& ny, double& t)
{
  const double dx = x1 - x0, dy = y1 - y0;
  const double len2 = dx * dx + dy * dy;
  if (len2 <= 0) {
    nx = x0;
    ny = y0;
    t = 0;
    return;
  }
  t = ((px - x0) * dx + (py - y0) * dy) / len2;
  t = std::max(0.0, std::min(1.0, t));
  nx = x0 + t * dx;
  ny = y0 + t * dy;
}

// Where two segments cross, if they do within their own extents.
bool segmentIntersection(double ax0, double ay0, double ax1, double ay1,
    double bx0, double by0, double bx1, double by1, double& ix, double& iy)
{
  const double r_x = ax1 - ax0, r_y = ay1 - ay0;
  const double s_x = bx1 - bx0, s_y = by1 - by0;
  const double denom = r_x * s_y - r_y * s_x;
  if (std::fabs(denom) < 1e-14)
    return false; // parallel or degenerate
  const double t = ((bx0 - ax0) * s_y - (by0 - ay0) * s_x) / denom;
  const double u = ((bx0 - ax0) * r_y - (by0 - ay0) * r_x) / denom;
  if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0)
    return false;
  ix = ax0 + t * r_x;
  iy = ay0 + t * r_y;
  return true;
}

bool segmentEndpoints(const FemmProblem& p, int i, double& x0, double& y0,
    double& x1, double& y1)
{
  if (i < 0 || i >= p.segments.size())
    return false;
  const FemmSegment& s = p.segments[i];
  if (s.n0 < 0 || s.n0 >= p.nodes.size() || s.n1 < 0 || s.n1 >= p.nodes.size())
    return false;
  x0 = p.nodes[s.n0].x;
  y0 = p.nodes[s.n0].y;
  x1 = p.nodes[s.n1].x;
  y1 = p.nodes[s.n1].y;
  return true;
}

// Is `angle` (radians) inside the arc's own sweep from n0, counterclockwise?
bool angleWithinArc(const FemmProblem& p, const FemmArcSegment& a,
    const Complex& centre, double angle)
{
  const Complex start(p.nodes[a.n0].x - centre.real(),
      p.nodes[a.n0].y - centre.imag());
  double from = std::arg(start);
  double sweep = a.arcLength * M_PI / 180.0;
  double delta = angle - from;
  while (delta < 0)
    delta += 2 * M_PI;
  while (delta > 2 * M_PI)
    delta -= 2 * M_PI;
  return delta <= sweep + 1e-12;
}

void collect(const FemmProblem& p, double cx, double cy, double radius,
    unsigned flags, bool hasRef, double rx, double ry,
    QVector<Candidate>& out)
{
  // --- endpoints ---------------------------------------------------------
  if (enabled(flags, SnapEngine::SnapEndpoint)) {
    for (int i = 0; i < p.nodes.size(); i++) {
      const double d = dist(cx, cy, p.nodes[i].x, p.nodes[i].y);
      if (d <= radius)
        out.push_back({ SnapEngine::SnapType::Endpoint, p.nodes[i].x,
            p.nodes[i].y, i, -1, d });
    }
  }

  // --- segment-derived ---------------------------------------------------
  for (int i = 0; i < p.segments.size(); i++) {
    double x0, y0, x1, y1;
    if (!segmentEndpoints(p, i, x0, y0, x1, y1))
      continue;

    if (enabled(flags, SnapEngine::SnapMidpoint)) {
      const double mx = (x0 + x1) / 2.0, my = (y0 + y1) / 2.0;
      const double d = dist(cx, cy, mx, my);
      if (d <= radius)
        out.push_back({ SnapEngine::SnapType::Midpoint, mx, my, i, -1, d });
    }

    if (enabled(flags, SnapEngine::SnapOnEdge)) {
      double nx, ny, t;
      nearestOnSegment(cx, cy, x0, y0, x1, y1, nx, ny, t);
      const double d = dist(cx, cy, nx, ny);
      if (d <= radius)
        out.push_back({ SnapEngine::SnapType::OnEdge, nx, ny, i, -1, d });
    }

    // Perpendicular from the point the user is drawing FROM: the foot on
    // this segment's infinite line, kept only if it lands on the segment.
    if (hasRef && enabled(flags, SnapEngine::SnapPerpendicular)) {
      double nx, ny, t;
      nearestOnSegment(rx, ry, x0, y0, x1, y1, nx, ny, t);
      if (t > 0.0 && t < 1.0) {
        const double d = dist(cx, cy, nx, ny);
        if (d <= radius)
          out.push_back({ SnapEngine::SnapType::Perpendicular, nx, ny, i, -1, d });
      }
    }

    if (enabled(flags, SnapEngine::SnapIntersection)) {
      for (int j = i + 1; j < p.segments.size(); j++) {
        double bx0, by0, bx1, by1;
        if (!segmentEndpoints(p, j, bx0, by0, bx1, by1))
          continue;
        double ix, iy;
        if (!segmentIntersection(x0, y0, x1, y1, bx0, by0, bx1, by1, ix, iy))
          continue;
        const double d = dist(cx, cy, ix, iy);
        if (d <= radius)
          out.push_back({ SnapEngine::SnapType::Intersection, ix, iy, i, j, d });
      }
    }
  }

  // --- arc-derived -------------------------------------------------------
  for (int i = 0; i < p.arcSegments.size(); i++) {
    const FemmArcSegment& a = p.arcSegments[i];
    if (a.n0 < 0 || a.n0 >= p.nodes.size() || a.n1 < 0 || a.n1 >= p.nodes.size())
      continue;
    Complex centre;
    double R = 0;
    if (!FemmProblemEdit::circleFromArc(p, a, centre, R) || R <= 0)
      continue;

    if (enabled(flags, SnapEngine::SnapCentre)) {
      const double d = dist(cx, cy, centre.real(), centre.imag());
      if (d <= radius)
        out.push_back({ SnapEngine::SnapType::Centre, centre.real(),
            centre.imag(), i, -1, d });
    }

    if (enabled(flags, SnapEngine::SnapQuadrant)) {
      for (int q = 0; q < 4; q++) {
        const double ang = q * M_PI / 2.0;
        if (!angleWithinArc(p, a, centre, ang))
          continue;
        const double qx = centre.real() + R * std::cos(ang);
        const double qy = centre.imag() + R * std::sin(ang);
        const double d = dist(cx, cy, qx, qy);
        if (d <= radius)
          out.push_back({ SnapEngine::SnapType::Quadrant, qx, qy, i, -1, d });
      }
    }

    if (enabled(flags, SnapEngine::SnapOnEdge)) {
      // Closest point on the circle, kept only if it lies on the arc.
      const double ang = std::atan2(cy - centre.imag(), cx - centre.real());
      if (angleWithinArc(p, a, centre, ang)) {
        const double nx = centre.real() + R * std::cos(ang);
        const double ny = centre.imag() + R * std::sin(ang);
        const double d = dist(cx, cy, nx, ny);
        if (d <= radius)
          out.push_back({ SnapEngine::SnapType::OnEdge, nx, ny, i, -1, d });
      }
    }

    // Tangent from the reference point: the two touch points exist only
    // when the reference is outside the circle.
    if (hasRef && enabled(flags, SnapEngine::SnapTangent)) {
      const double dc = dist(rx, ry, centre.real(), centre.imag());
      if (dc > R) {
        const double base = std::atan2(ry - centre.imag(), rx - centre.real());
        const double spread = std::acos(R / dc);
        for (int sgn = -1; sgn <= 1; sgn += 2) {
          const double ang = base + sgn * spread;
          if (!angleWithinArc(p, a, centre, ang))
            continue;
          const double tx = centre.real() + R * std::cos(ang);
          const double ty = centre.imag() + R * std::sin(ang);
          const double d = dist(cx, cy, tx, ty);
          if (d <= radius)
            out.push_back({ SnapEngine::SnapType::Tangent, tx, ty, i, -1, d });
        }
      }
    }
  }
}

} // namespace

const char* SnapEngine::name(SnapType type)
{
  switch (type) {
  case SnapType::Endpoint: return "Endpoint";
  case SnapType::Intersection: return "Intersection";
  case SnapType::Centre: return "Centre";
  case SnapType::Midpoint: return "Midpoint";
  case SnapType::Quadrant: return "Quadrant";
  case SnapType::Perpendicular: return "Perpendicular";
  case SnapType::Tangent: return "Tangent";
  case SnapType::OnEdge: return "On Edge";
  case SnapType::Grid: return "Grid";
  case SnapType::None: return "";
  }
  return "";
}

SnapEngine::SnapResult SnapEngine::findSnap(const FemmProblem& p,
    double cursorX, double cursorY, double captureRadius, unsigned flags,
    double gridSize, bool hasReference, double referenceX, double referenceY)
{
  SnapResult result;
  if (captureRadius <= 0)
    captureRadius = 0;

  QVector<Candidate> candidates;
  if (captureRadius > 0) {
    collect(p, cursorX, cursorY, captureRadius, flags, hasReference,
        referenceX, referenceY, candidates);
  }

  if (!candidates.isEmpty()) {
    // Priority first, distance only as a tie-break WITHIN a type. A
    // midpoint 1 unit away must not beat an endpoint 2 units away: the
    // user hovering near a corner means the corner, and a purely
    // nearest-wins rule makes the specific targets unreachable wherever
    // a generic one happens to be closer -- which, for OnEdge, is almost
    // everywhere along an edge.
    const Candidate* best = nullptr;
    for (const Candidate& c : candidates) {
      if (!best || (int)c.type < (int)best->type
          || ((int)c.type == (int)best->type && c.distance < best->distance))
        best = &c;
    }
    result.type = best->type;
    result.x = best->x;
    result.y = best->y;
    result.index = best->index;
    result.otherIndex = best->otherIndex;
    return result;
  }

  // Grid last, and only if nothing geometric was in range. Composing
  // rather than replacing: grid snap keeps working exactly as it did
  // wherever there is no geometry to catch.
  if (enabled(flags, SnapGrid) && gridSize > 0) {
    result.type = SnapType::Grid;
    result.x = std::round(cursorX / gridSize) * gridSize;
    result.y = std::round(cursorY / gridSize) * gridSize;
    result.index = -1;
    return result;
  }

  result.type = SnapType::None;
  result.x = cursorX;
  result.y = cursorY;
  return result;
}
