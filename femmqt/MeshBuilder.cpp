#define _USE_MATH_DEFINES

#include "MeshBuilder.h"

#include "FemmProblem.h"

#include <QFile>
#include <QTextStream>
#include <QVector>

#include <cmath>

namespace {

// femm/StdAfx.h's compile-time constants (femm/StdAfx.h:96-103), ported
// verbatim -- see there for their meaning.
constexpr double kBoundingBoxFraction = 100.0;
constexpr double kLineFraction = 500.0;

QString g17(double v)
{
  return QString::number(v, 'g', 17);
}

struct WorkNode {
  double x, y;
  int marker = 0; // 0 = none, else already the .poly-encoded (index+2) value
};

struct WorkSegment {
  int n0, n1;
  int marker = 0; // 0 = none, else already the .poly-encoded -(index+2) value
};

// Mirrors CFemmeDoc::GetCircle (femm/FemmeDoc.cpp) -- also duplicated (in
// the same, deliberately small form) in GeometryScene.cpp for rendering;
// kept as two small copies rather than a shared header since each call
// site's surrounding code differs enough that a shared utility would
// mostly just be this one function.
bool arcCircle(double x0, double y0, double x1, double y1, double arcLengthDeg,
    double& cx, double& cy, double& R)
{
  double dx = x1 - x0, dy = y1 - y0;
  double d = std::hypot(dx, dy);
  if (d <= 0)
    return false;
  double tta = arcLengthDeg * M_PI / 180.0;
  double s = std::sin(tta / 2.0);
  if (std::abs(s) < 1e-12)
    return false;
  R = d / (2.0 * s);
  double tx = dx / d, ty = dy / d;
  double h = std::sqrt(std::max(0.0, R * R - d * d / 4.0));
  cx = x0 + (d / 2.0 * tx - h * ty);
  cy = y0 + (d / 2.0 * ty + h * tx);
  return true;
}

} // namespace

bool MeshBuilder::writePolyAndPbc(const FemmProblem& p, const QString& rootPath, QString& errorMessage)
{
  QVector<WorkNode> nodes;
  QVector<WorkSegment> segments;

  // calculate length used to kludge fine meshing near input node points
  // (femm/writepoly.cpp:117-122)
  double z = 0.0;
  for (const FemmSegment& s : p.segments) {
    if (s.n0 < 0 || s.n0 >= p.nodes.size() || s.n1 < 0 || s.n1 >= p.nodes.size())
      continue;
    double dx = p.nodes[s.n1].x - p.nodes[s.n0].x;
    double dy = p.nodes[s.n1].y - p.nodes[s.n0].y;
    z += std::hypot(dx, dy) / (double)p.segments.size();
  }
  double dL = z / kLineFraction;

  // copy node list as-is, translating each node's already-resolved
  // 1-based pointPropIndex (0 = none) into .poly's (index0based+2) point-
  // marker encoding. This is a point-property reference, not a boundary
  // one -- see FemmNode::pointPropIndex's comment (FemmProblem.h) --
  // matching femm/writepoly.cpp:259-260's nodeproplist lookup.
  for (const FemmNode& n : p.nodes) {
    WorkNode wn;
    wn.x = n.x;
    wn.y = n.y;
    wn.marker = (n.pointPropIndex == 0) ? 0 : (n.pointPropIndex + 1);
    nodes.push_back(wn);
  }

  // discretize input segments (femm/writepoly.cpp:128-204)
  for (const FemmSegment& s : p.segments) {
    if (s.n0 < 0 || s.n0 >= p.nodes.size() || s.n1 < 0 || s.n1 >= p.nodes.size())
      continue;
    double x0 = p.nodes[s.n0].x, y0 = p.nodes[s.n0].y;
    double x1 = p.nodes[s.n1].x, y1 = p.nodes[s.n1].y;
    double dx = x1 - x0, dy = y1 - y0;
    double lineLen = std::hypot(dx, dy);
    int segMarker = (s.boundaryMarker == 0) ? 0 : -(s.boundaryMarker + 1);

    int k;
    if (s.maxSideLength < 0)
      k = 1;
    else if (s.maxSideLength <= 0 || lineLen <= 0)
      k = 1; // defensive: avoid div-by-zero on a degenerate/unconstrained segment
    else
      k = (int)std::ceil(lineLen / s.maxSideLength);

    if (k <= 1) {
      if (lineLen < 3.0 * dL || !p.smartMesh) {
        segments.push_back({ s.n0, s.n1, segMarker });
      } else {
        // add extra points at a distance of dL from the ends of the line,
        // so triangle.exe finely meshes near corners.
        double ux = dx / lineLen, uy = dy / lineLen;
        double p0x = x0 + dL * ux, p0y = y0 + dL * uy;
        double p1x = x1 - dL * ux, p1y = y1 - dL * uy;
        int p0idx = nodes.size();
        nodes.push_back({ p0x, p0y, 0 });
        int p1idx = nodes.size();
        nodes.push_back({ p1x, p1y, 0 });
        segments.push_back({ s.n0, p0idx, segMarker });
        segments.push_back({ p0idx, p1idx, segMarker });
        segments.push_back({ p1idx, s.n1, segMarker });
      }
    } else {
      int prevIdx = s.n0;
      for (int j = 0; j < k; j++) {
        if (j == k - 1) {
          segments.push_back({ prevIdx, s.n1, segMarker });
        } else {
          double t = (double)(j + 1) / (double)k;
          int idx = nodes.size();
          nodes.push_back({ x0 + dx * t, y0 + dy * t, 0 });
          segments.push_back({ prevIdx, idx, segMarker });
          prevIdx = idx;
        }
      }
    }
  }

  // discretize input arc segments (femm/writepoly.cpp:206-243)
  for (const FemmArcSegment& arc : p.arcSegments) {
    if (arc.n0 < 0 || arc.n0 >= p.nodes.size() || arc.n1 < 0 || arc.n1 >= p.nodes.size())
      continue;
    double x0 = p.nodes[arc.n0].x, y0 = p.nodes[arc.n0].y;
    double x1 = p.nodes[arc.n1].x, y1 = p.nodes[arc.n1].y;
    int segMarker = (arc.boundaryMarker == 0) ? 0 : -(arc.boundaryMarker + 1);

    double cx, cy, R;
    if (!arcCircle(x0, y0, x1, y1, arc.arcLength, cx, cy, R)) {
      segments.push_back({ arc.n0, arc.n1, segMarker }); // degenerate arc: fall back to a straight chord
      continue;
    }

    double maxSideLength = (arc.maxSideLength > 0) ? arc.maxSideLength : 1.0;
    int k = (int)std::ceil(arc.arcLength / maxSideLength);
    if (k < 1)
      k = 1;

    if (k == 1) {
      segments.push_back({ arc.n0, arc.n1, segMarker });
    } else {
      double stepAngleRad = arc.arcLength * M_PI / ((double)k * 180.0);
      double rotCos = std::cos(stepAngleRad), rotSin = std::sin(stepAngleRad);
      double curX = x0, curY = y0;
      int prevIdx = arc.n0;
      for (int j = 0; j < k; j++) {
        double rx = curX - cx, ry = curY - cy;
        curX = cx + (rx * rotCos - ry * rotSin);
        curY = cy + (rx * rotSin + ry * rotCos);
        if (j == k - 1) {
          segments.push_back({ prevIdx, arc.n1, segMarker });
        } else {
          int idx = nodes.size();
          nodes.push_back({ curX, curY, 0 });
          segments.push_back({ prevIdx, idx, segMarker });
          prevIdx = idx;
        }
      }
    }
  }

  QFile polyFile(rootPath + ".poly");
  if (!polyFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    errorMessage = QStringLiteral("Could not write \"%1\".").arg(polyFile.fileName());
    return false;
  }
  QTextStream out(&polyFile);

  out << nodes.size() << "\t2\t0\t1\n";
  for (int i = 0; i < nodes.size(); i++)
    out << i << "\t" << g17(nodes[i].x) << "\t" << g17(nodes[i].y) << "\t" << nodes[i].marker << "\n";

  out << segments.size() << "\t1\n";
  for (int i = 0; i < segments.size(); i++)
    out << i << "\t" << segments[i].n0 << "\t" << segments[i].n1 << "\t" << segments[i].marker << "\n";

  // holes (femm/writepoly.cpp:274-283)
  QVector<int> holeIndices;
  for (int i = 0; i < p.blockLabels.size(); i++)
    if (p.blockLabels[i].blockTypeIndex < 0)
      holeIndices.push_back(i);
  out << holeIndices.size() << "\n";
  for (int k = 0; k < holeIndices.size(); k++) {
    const FemmBlockLabel& b = p.blockLabels[holeIndices[k]];
    out << k << "\t" << g17(b.x) << "\t" << g17(b.y) << "\n";
  }

  // default mesh size for block labels without an explicit one
  // (femm/writepoly.cpp:285-306)
  double defaultMeshSize = -1.0;
  if (nodes.size() > 1) {
    double xmin = nodes[0].x, xmax = xmin, ymin = nodes[0].y, ymax = ymin;
    for (const WorkNode& n : nodes) {
      xmin = std::min(xmin, n.x);
      xmax = std::max(xmax, n.x);
      ymin = std::min(ymin, n.y);
      ymax = std::max(ymax, n.y);
    }
    double diag = std::hypot(xmax - xmin, ymax - ymin);
    defaultMeshSize = p.smartMesh ? std::pow(diag / kBoundingBoxFraction, 2.0) : diag;
  }

  // regional attributes (femm/writepoly.cpp:308-319)
  QVector<int> regionIndices;
  for (int i = 0; i < p.blockLabels.size(); i++)
    if (p.blockLabels[i].blockTypeIndex >= 0)
      regionIndices.push_back(i);
  out << regionIndices.size() << "\n";
  for (int k = 0; k < regionIndices.size(); k++) {
    const FemmBlockLabel& b = p.blockLabels[regionIndices[k]];
    double meshSize = (b.maxArea > 0 && b.maxArea < defaultMeshSize) ? b.maxArea : defaultMeshSize;
    out << k << "\t" << g17(b.x) << "\t" << g17(b.y) << "\t" << (k + 1) << "\t" << g17(meshSize) << "\n";
  }
  polyFile.close();

  // trivial .pbc (femm/writepoly.cpp:322-329)
  QFile pbcFile(rootPath + ".pbc");
  if (!pbcFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    errorMessage = QStringLiteral("Could not write \"%1\".").arg(pbcFile.fileName());
    return false;
  }
  QTextStream pbcOut(&pbcFile);
  pbcOut << "0\n0\n";
  pbcFile.close();

  return true;
}
