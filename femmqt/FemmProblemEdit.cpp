#define _USE_MATH_DEFINES

#include "FemmProblemEdit.h"

#include "FemmProblem.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <complex>

int FemmProblemEdit::addNode(FemmProblem& p, double x, double y)
{
  FemmNode n;
  n.x = x;
  n.y = y;
  p.nodes.push_back(n);
  return p.nodes.size() - 1;
}

int FemmProblemEdit::addSegment(FemmProblem& p, int n0, int n1)
{
  FemmSegment s;
  s.n0 = n0;
  s.n1 = n1;
  p.segments.push_back(s);
  return p.segments.size() - 1;
}

int FemmProblemEdit::addArcSegment(FemmProblem& p, int n0, int n1, double arcLengthDeg, double maxSideLengthDeg)
{
  FemmArcSegment a;
  a.n0 = n0;
  a.n1 = n1;
  a.arcLength = arcLengthDeg;
  a.maxSideLength = maxSideLengthDeg;
  a.mySideLength = maxSideLengthDeg; // "regular case" default -- see MeshBuilder.cpp's writepoly.cpp reference comment
  p.arcSegments.push_back(a);
  return p.arcSegments.size() - 1;
}

int FemmProblemEdit::addBlockLabel(FemmProblem& p, double x, double y)
{
  FemmBlockLabel b;
  b.x = x;
  b.y = y;
  b.blockTypeIndex = -1; // hole by default -- caller/dialog assigns a
                         // real material afterward; matches this phase's
                         // "no default material picker yet" scope.
  p.blockLabels.push_back(b);
  return p.blockLabels.size() - 1;
}

void FemmProblemEdit::deleteNode(FemmProblem& p, int nodeIndex)
{
  if (nodeIndex < 0 || nodeIndex >= p.nodes.size())
    return;

  QVector<FemmSegment> keptSegments;
  for (const FemmSegment& s : p.segments) {
    if (s.n0 == nodeIndex || s.n1 == nodeIndex)
      continue;
    FemmSegment s2 = s;
    if (s2.n0 > nodeIndex)
      s2.n0--;
    if (s2.n1 > nodeIndex)
      s2.n1--;
    keptSegments.push_back(s2);
  }
  p.segments = keptSegments;

  QVector<FemmArcSegment> keptArcs;
  for (const FemmArcSegment& a : p.arcSegments) {
    if (a.n0 == nodeIndex || a.n1 == nodeIndex)
      continue;
    FemmArcSegment a2 = a;
    if (a2.n0 > nodeIndex)
      a2.n0--;
    if (a2.n1 > nodeIndex)
      a2.n1--;
    keptArcs.push_back(a2);
  }
  p.arcSegments = keptArcs;

  p.nodes.remove(nodeIndex);
}

void FemmProblemEdit::deleteSegment(FemmProblem& p, int segmentIndex)
{
  if (segmentIndex < 0 || segmentIndex >= p.segments.size())
    return;
  p.segments.remove(segmentIndex);
}

void FemmProblemEdit::deleteArcSegment(FemmProblem& p, int arcIndex)
{
  if (arcIndex < 0 || arcIndex >= p.arcSegments.size())
    return;
  p.arcSegments.remove(arcIndex);
}

void FemmProblemEdit::deleteBlockLabel(FemmProblem& p, int blockLabelIndex)
{
  if (blockLabelIndex < 0 || blockLabelIndex >= p.blockLabels.size())
    return;
  p.blockLabels.remove(blockLabelIndex);
}

int FemmProblemEdit::countPointPropReferences(const FemmProblem& p, int index)
{
  int marker = index + 1;
  int n = 0;
  for (const FemmNode& node : p.nodes)
    if (node.pointPropIndex == marker)
      n++;
  return n;
}

void FemmProblemEdit::deletePointProp(FemmProblem& p, int index)
{
  if (index < 0 || index >= p.pointProps.size())
    return;
  int marker = index + 1;
  for (FemmNode& node : p.nodes) {
    if (node.pointPropIndex == marker)
      node.pointPropIndex = 0;
    else if (node.pointPropIndex > marker)
      node.pointPropIndex--;
  }
  p.pointProps.remove(index);
}

int FemmProblemEdit::countBoundaryPropReferences(const FemmProblem& p, int index)
{
  int marker = index + 1;
  int n = 0;
  for (const FemmSegment& s : p.segments)
    if (s.boundaryMarker == marker)
      n++;
  for (const FemmArcSegment& a : p.arcSegments)
    if (a.boundaryMarker == marker)
      n++;
  return n;
}

void FemmProblemEdit::deleteBoundaryProp(FemmProblem& p, int index)
{
  if (index < 0 || index >= p.boundaryProps.size())
    return;
  int marker = index + 1;
  for (FemmSegment& s : p.segments) {
    if (s.boundaryMarker == marker)
      s.boundaryMarker = 0;
    else if (s.boundaryMarker > marker)
      s.boundaryMarker--;
  }
  for (FemmArcSegment& a : p.arcSegments) {
    if (a.boundaryMarker == marker)
      a.boundaryMarker = 0;
    else if (a.boundaryMarker > marker)
      a.boundaryMarker--;
  }
  p.boundaryProps.remove(index);
}

int FemmProblemEdit::countMaterialPropReferences(const FemmProblem& p, int index)
{
  int marker = index + 1;
  int n = 0;
  for (const FemmBlockLabel& b : p.blockLabels)
    if (b.blockTypeIndex == marker)
      n++;
  return n;
}

void FemmProblemEdit::deleteMaterialProp(FemmProblem& p, int index)
{
  if (index < 0 || index >= p.materialProps.size())
    return;
  int marker = index + 1;
  for (FemmBlockLabel& b : p.blockLabels) {
    if (b.blockTypeIndex == marker)
      b.blockTypeIndex = -1; // reverts to a hole -- see this function's header comment
    else if (b.blockTypeIndex > marker)
      b.blockTypeIndex--;
  }
  p.materialProps.remove(index);
}

int FemmProblemEdit::countCircuitPropReferences(const FemmProblem& p, int index)
{
  int marker = index + 1;
  int n = 0;
  for (const FemmBlockLabel& b : p.blockLabels)
    if (b.circuitIndex == marker)
      n++;
  return n;
}

void FemmProblemEdit::deleteCircuitProp(FemmProblem& p, int index)
{
  if (index < 0 || index >= p.circuitProps.size())
    return;
  int marker = index + 1;
  for (FemmBlockLabel& b : p.blockLabels) {
    if (b.circuitIndex == marker)
      b.circuitIndex = 0;
    else if (b.circuitIndex > marker)
      b.circuitIndex--;
  }
  p.circuitProps.remove(index);
}

void FemmProblemEdit::moveSelected(FemmProblem& p, double dx, double dy)
{
  for (FemmNode& n : p.nodes) {
    if (n.isSelected) {
      n.x += dx;
      n.y += dy;
    }
  }
  for (FemmBlockLabel& b : p.blockLabels) {
    if (b.isSelected) {
      b.x += dx;
      b.y += dy;
    }
  }
}

void FemmProblemEdit::copySelected(FemmProblem& p, double dx, double dy)
{
  // Old node index -> new node index, for remapping copied segments/arcs.
  QHash<int, int> nodeMap;
  int originalNodeCount = p.nodes.size();
  for (int i = 0; i < originalNodeCount; i++) {
    if (!p.nodes[i].isSelected)
      continue;
    FemmNode copy = p.nodes[i];
    copy.x += dx;
    copy.y += dy;
    copy.isSelected = false;
    nodeMap[i] = p.nodes.size();
    p.nodes.push_back(copy);
  }

  int originalSegmentCount = p.segments.size();
  for (int i = 0; i < originalSegmentCount; i++) {
    const FemmSegment& s = p.segments[i];
    if (!s.isSelected || !nodeMap.contains(s.n0) || !nodeMap.contains(s.n1))
      continue;
    FemmSegment copy = s;
    copy.n0 = nodeMap[s.n0];
    copy.n1 = nodeMap[s.n1];
    copy.isSelected = false;
    p.segments.push_back(copy);
  }

  int originalArcCount = p.arcSegments.size();
  for (int i = 0; i < originalArcCount; i++) {
    const FemmArcSegment& a = p.arcSegments[i];
    if (!a.isSelected || !nodeMap.contains(a.n0) || !nodeMap.contains(a.n1))
      continue;
    FemmArcSegment copy = a;
    copy.n0 = nodeMap[a.n0];
    copy.n1 = nodeMap[a.n1];
    copy.isSelected = false;
    p.arcSegments.push_back(copy);
  }

  int originalBlockLabelCount = p.blockLabels.size();
  for (int i = 0; i < originalBlockLabelCount; i++) {
    if (!p.blockLabels[i].isSelected)
      continue;
    FemmBlockLabel copy = p.blockLabels[i];
    copy.x += dx;
    copy.y += dy;
    copy.isSelected = false;
    p.blockLabels.push_back(copy);
  }
}

void FemmProblemEdit::scaleSelected(FemmProblem& p, double baseX, double baseY, double factor)
{
  for (FemmNode& n : p.nodes) {
    if (n.isSelected) {
      n.x = baseX + (n.x - baseX) * factor;
      n.y = baseY + (n.y - baseY) * factor;
    }
  }
  for (FemmBlockLabel& b : p.blockLabels) {
    if (b.isSelected) {
      b.x = baseX + (b.x - baseX) * factor;
      b.y = baseY + (b.y - baseY) * factor;
    }
  }
}

namespace {
void reflectPoint(double& x, double& y, double x0, double y0, double ux, double uy)
{
  // ux,uy is the mirror line's unit direction vector; (x0,y0) is any point
  // on it. Standard reflect-across-a-line-through-a-point formula: subtract
  // the point on the line, remove twice the perpendicular component, add
  // the point back.
  double dx = x - x0, dy = y - y0;
  double proj = dx * ux + dy * uy;
  double perpX = dx - proj * ux, perpY = dy - proj * uy;
  x -= 2 * perpX;
  y -= 2 * perpY;
}
}

void FemmProblemEdit::mirrorSelected(FemmProblem& p, double x0, double y0, double x1, double y1)
{
  double dx = x1 - x0, dy = y1 - y0;
  double len = std::hypot(dx, dy);
  if (len <= 0)
    return;
  double ux = dx / len, uy = dy / len;

  for (FemmNode& n : p.nodes)
    if (n.isSelected)
      reflectPoint(n.x, n.y, x0, y0, ux, uy);
  for (FemmBlockLabel& b : p.blockLabels)
    if (b.isSelected)
      reflectPoint(b.x, b.y, x0, y0, ux, uy);
}

namespace {
using Complex = std::complex<double>;

// Mirrors CFemmeDoc::GetCircle (femm/FemmeDoc.cpp) -- same formula as
// GeometryScene.cpp's own local arcGeometry(), just returning center+
// radius as a Complex/double pair instead of also computing a start
// angle (createRadius has no use for one).
bool circleFromArc(const FemmProblem& p, const FemmArcSegment& arc, Complex& c, double& R)
{
  double x0 = p.nodes[arc.n0].x, y0 = p.nodes[arc.n0].y;
  double x1 = p.nodes[arc.n1].x, y1 = p.nodes[arc.n1].y;
  double dx = x1 - x0, dy = y1 - y0;
  double d = std::hypot(dx, dy);
  if (d <= 0)
    return false;
  double tta = arc.arcLength * M_PI / 180.0;
  double s = std::sin(tta / 2.0);
  if (std::abs(s) < 1e-12)
    return false;
  R = d / (2.0 * s);
  double tx = dx / d, ty = dy / d;
  double h = std::sqrt(std::max(0.0, R * R - d * d / 4.0));
  c = Complex(x0 + (d / 2.0 * tx - h * ty), y0 + (d / 2.0 * ty + h * tx));
  return true;
}

// Mirrors CFemmeDoc::ShortestDistanceFromArc (femm/FemmeDoc.cpp:692) --
// distance from `pt` to the nearest point actually on the arc (not the
// full circle): if the closest point on the circle falls within the
// arc's own angular span, that; otherwise whichever endpoint is closer.
double shortestDistanceFromArc(Complex pt, const FemmProblem& p, const FemmArcSegment& arc)
{
  Complex a0(p.nodes[arc.n0].x, p.nodes[arc.n0].y);
  Complex c;
  double R;
  if (!circleFromArc(p, arc, c, R))
    return std::abs(pt - a0);

  double d = std::abs(pt - c);
  if (d == 0)
    return R;

  Complex t = (pt - c) / d;
  double l = std::abs(pt - c - R * t);
  double z = std::arg(t / (a0 - c)) * 180.0 / M_PI;
  if (z > 0 && z < arc.arcLength)
    return l;

  Complex a1(p.nodes[arc.n1].x, p.nodes[arc.n1].y);
  return std::min(std::abs(pt - a0), std::abs(pt - a1));
}

// Mirrors CFemmeDoc::ShortestDistance (femm/FemmeDoc.cpp:718) -- distance
// from (px,py) to the nearest point on segment `seg` (clamped to the
// segment's own extent, not the infinite line through it).
double shortestDistanceFromSegment(double px, double py, const FemmProblem& p, const FemmSegment& seg)
{
  double x0 = p.nodes[seg.n0].x, y0 = p.nodes[seg.n0].y;
  double x1 = p.nodes[seg.n1].x, y1 = p.nodes[seg.n1].y;
  double denom = (x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0);
  double t = denom > 0 ? ((px - x0) * (x1 - x0) + (py - y0) * (y1 - y0)) / denom : 0.0;
  t = std::max(0.0, std::min(1.0, t));
  double xx = x0 + t * (x1 - x0), yy = y0 + t * (y1 - y0);
  return std::hypot(px - xx, py - yy);
}
} // namespace

bool FemmProblemEdit::canCreateRadius(const FemmProblem& p, int nodeIndex)
{
  int count = 0;
  for (const FemmSegment& s : p.segments)
    if (s.n0 == nodeIndex || s.n1 == nodeIndex)
      count++;
  for (const FemmArcSegment& a : p.arcSegments)
    if (a.n0 == nodeIndex || a.n1 == nodeIndex)
      count++;
  return count == 2;
}

bool FemmProblemEdit::createRadius(FemmProblem& p, int n, double r)
{
  if (r <= 0 || n < 0 || n >= p.nodes.size())
    return false;

  QVector<int> segIdx, arcIdx;
  for (int k = 0; k < p.segments.size(); k++)
    if (p.segments[k].n0 == n || p.segments[k].n1 == n)
      segIdx.push_back(k);
  for (int k = 0; k < p.arcSegments.size(); k++)
    if (p.arcSegments[k].n0 == n || p.arcSegments[k].n1 == n)
      arcIdx.push_back(k);
  if (segIdx.size() + arcIdx.size() != 2)
    return false;

  const Complex p0(p.nodes[n].x, p.nodes[n].y);
  constexpr double kTol = 1.0 / 10000.0; // matches femm/FemmeDoc.cpp's fixed r/10000 tolerance, scaled by r below

  if (segIdx.size() == 2) {
    // Two lines (femm/FemmeDoc.cpp's CreateRadius, "case 2").
    const FemmSegment* seg0 = &p.segments[segIdx[0]];
    const FemmSegment* seg1 = &p.segments[segIdx[1]];
    Complex p1 = (seg0->n0 == n) ? Complex(p.nodes[seg0->n1].x, p.nodes[seg0->n1].y) : Complex(p.nodes[seg0->n0].x, p.nodes[seg0->n0].y);
    Complex p2 = (seg1->n0 == n) ? Complex(p.nodes[seg1->n1].x, p.nodes[seg1->n1].y) : Complex(p.nodes[seg1->n0].x, p.nodes[seg1->n0].y);

    double phi = std::arg((p2 - p0) / (p1 - p0));
    if (std::fabs(phi) > 179.0 * M_PI / 180.0)
      return false;

    if (phi < 0) {
      std::swap(p1, p2);
      std::swap(seg0, seg1);
      phi = std::fabs(phi);
    }

    double len = r / std::tan(phi / 2.0);
    if (std::abs(p1 - p0) < len || std::abs(p2 - p0) < len)
      return false;

    Complex t1 = len * (p1 - p0) / std::abs(p1 - p0) + p0;
    Complex t2 = len * (p2 - p0) / std::abs(p2 - p0) + p0;

    int boundaryMarker = seg0->boundaryMarker;
    int group = seg0->inGroup;

    int n1idx = addNode(p, t1.real(), t1.imag());
    int n2idx = addNode(p, t2.real(), t2.imag());
    deleteNode(p, n);
    if (n1idx > n)
      n1idx--;
    if (n2idx > n)
      n2idx--;

    FemmArcSegment ar;
    ar.n0 = n2idx; // matches CreateRadius: ar.n0 = ClosestNode(p2), ar.n1 = ClosestNode(p1)
    ar.n1 = n1idx;
    ar.arcLength = 180.0 - phi * 180.0 / M_PI;
    ar.boundaryMarker = boundaryMarker;
    ar.inGroup = group;
    p.arcSegments.push_back(ar);
    return true;
  }

  if (segIdx.size() == 1 && arcIdx.size() == 1) {
    // One line, one arc (femm/FemmeDoc.cpp's CreateRadius, "case 0").
    const FemmArcSegment& arc = p.arcSegments[arcIdx[0]];
    const FemmSegment& seg = p.segments[segIdx[0]];

    Complex c;
    double rc;
    if (!circleFromArc(p, arc, c, rc))
      return false;

    Complex p1 = (seg.n0 == n) ? Complex(p.nodes[seg.n1].x, p.nodes[seg.n1].y) : Complex(p.nodes[seg.n0].x, p.nodes[seg.n0].y);
    double lenP1P0 = std::abs(p1 - p0);
    if (lenP1P0 <= 0)
      return false;

    Complex u = (p1 - p0) / lenP1P0; // unit vector along the line
    Complex q = p0 + u * ((c - p0) / u).real(); // closest point on line to the arc's center
    Complex normal = Complex(0, 1) * u;

    Complex candP[4] = { q + r * normal, q - r * normal, q + r * normal, q - r * normal };
    double candR[4] = { rc + r, rc + r, rc - r, rc - r };

    Complex v[8];
    int nv = 0;
    for (int k = 0; k < 4; k++) {
      double b = candR[k] * candR[k] - std::norm(candP[k] - c);
      if (b < 0)
        continue;
      b = std::sqrt(b);
      double dabs = std::abs(candP[k] - c);
      if (dabs <= 0)
        continue;
      v[nv++] = candP[k] + Complex(0, 1) * b * (candP[k] - c) / dabs;
      v[nv++] = candP[k] - Complex(0, 1) * b * (candP[k] - c) / dabs;
    }

    Complex winI1[2], winI2[2], winV[2];
    int m = 0;
    for (int k = 0; k < nv && m < 2; k++) {
      Complex i1 = p0 + u * ((v[k] - p0) / u).real();
      double vcDist = std::abs(v[k] - c);
      if (vcDist <= 0)
        continue;
      Complex i2 = c + rc * (v[k] - c) / vcDist;
      if (shortestDistanceFromArc(i2, p, arc) < r * kTol
          && shortestDistanceFromSegment(i1.real(), i1.imag(), p, seg) < r * kTol
          && std::abs(i1 - i2) > r * kTol) {
        winI1[m] = i1;
        winI2[m] = i2;
        winV[m] = v[k];
        m++;
      }
    }
    if (m == 0)
      return false;
    int win = (m > 1 && std::abs(winV[1] - p0) < std::abs(winV[0] - p0)) ? 1 : 0;

    Complex i1 = winI1[win], i2 = winI2[win], vw = winV[win];
    int boundaryMarker = arc.boundaryMarker;
    int group = arc.inGroup;

    int n1idx = addNode(p, i1.real(), i1.imag());
    int n2idx = addNode(p, i2.real(), i2.imag());
    deleteNode(p, n);
    if (n1idx > n)
      n1idx--;
    if (n2idx > n)
      n2idx--;

    double phi = std::arg((i2 - vw) / (i1 - vw));
    if (phi < 0) {
      std::swap(n1idx, n2idx);
      phi = std::fabs(phi);
    }

    FemmArcSegment ar;
    ar.n0 = n1idx;
    ar.n1 = n2idx;
    ar.arcLength = phi * 180.0 / M_PI;
    ar.boundaryMarker = boundaryMarker;
    ar.inGroup = group;
    p.arcSegments.push_back(ar);
    return true;
  }

  if (arcIdx.size() == 2) {
    // Two arcs (femm/FemmeDoc.cpp's CreateRadius, "case -2").
    const FemmArcSegment& arc0 = p.arcSegments[arcIdx[0]];
    const FemmArcSegment& arc1 = p.arcSegments[arcIdx[1]];
    Complex c1, c2;
    double r1, r2;
    if (!circleFromArc(p, arc0, c1, r1) || !circleFromArc(p, arc1, c2, r2))
      return false;
    double c = std::abs(c2 - c1);
    if (c <= 0)
      return false;
    Complex dir = (c2 - c1) / c;

    double a[8], b[8];
    a[0] = r1 + r; b[0] = r2 + r;
    a[1] = r1 + r; b[1] = r2 + r;
    a[2] = r1 - r; b[2] = r2 - r;
    a[3] = r1 - r; b[3] = r2 - r;
    a[4] = r1 - r; b[4] = r2 + r;
    a[5] = r1 - r; b[5] = r2 + r;
    a[6] = r1 + r; b[6] = r2 - r;
    a[7] = r1 + r; b[7] = r2 - r;

    Complex cand[8];
    for (int k = 0; k < 8; k += 2) {
      double x = (b[k] * b[k] + c * c - a[k] * a[k]) / (2.0 * c * c);
      double d = std::sqrt(b[k] * b[k] - x * x * c * c); // NaN for no-solution cases -- matches femm/FemmeDoc.cpp, which relies on the same IEEE NaN propagation to naturally fail every later comparison for these
      cand[k] = ((1 - x) * c + Complex(0, 1) * d) * dir + c1;
      cand[k + 1] = ((1 - x) * c - Complex(0, 1) * d) * dir + c1;
    }

    const Complex c0 = p0;
    Complex winI1[2], winI2[2], winP[2];
    int j = 0;
    for (int k = 0; k < 8 && j < 2; k++) {
      double d1abs = std::abs(cand[k] - c1);
      double d2abs = std::abs(cand[k] - c2);
      if (!(d1abs > 0) || !(d2abs > 0)) // catches NaN candidates too (all comparisons with NaN are false)
        continue;
      Complex i1 = c1 + r1 * (cand[k] - c1) / d1abs;
      Complex i2 = c2 + r2 * (cand[k] - c2) / d2abs;
      if (shortestDistanceFromArc(i1, p, arc0) < r * kTol
          && shortestDistanceFromArc(i2, p, arc1) < r * kTol
          && std::abs(i1 - i2) > r * kTol) {
        winI1[j] = i1;
        winI2[j] = i2;
        winP[j] = cand[k];
        j++;
      }
    }
    if (j == 0)
      return false;
    int win = (j > 1 && std::abs(winP[1] - c0) < std::abs(winP[0] - c0)) ? 1 : 0;

    Complex i1 = winI1[win], i2 = winI2[win], vw = winP[win];
    int boundaryMarker = arc0.boundaryMarker;
    int group = arc0.inGroup;

    int n1idx = addNode(p, i1.real(), i1.imag());
    int n2idx = addNode(p, i2.real(), i2.imag());
    deleteNode(p, n);
    if (n1idx > n)
      n1idx--;
    if (n2idx > n)
      n2idx--;

    double phi = std::arg((i2 - vw) / (i1 - vw));
    if (phi < 0) {
      std::swap(n1idx, n2idx);
      phi = std::fabs(phi);
    }

    FemmArcSegment ar;
    ar.n0 = n1idx;
    ar.n1 = n2idx;
    ar.arcLength = phi * 180.0 / M_PI;
    ar.boundaryMarker = boundaryMarker;
    ar.inGroup = group;
    p.arcSegments.push_back(ar);
    return true;
  }

  return false;
}
