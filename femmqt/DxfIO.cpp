#define _USE_MATH_DEFINES

#include "DxfIO.h"

#include <QFileInfo>

#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>

using Complex = std::complex<double>;

namespace {

QVector<QString> g_layerList;

int layerToGroup(const char* layerName)
{
  QString name = QString::fromLocal8Bit(layerName);
  for (int j = 0; j < g_layerList.size(); j++) {
    if (g_layerList[j] == name)
      return j;
  }
  return 0;
}

// Reads one DXF "group code" line + its value line, exactly matching
// femm/MOVECOPY.CPP's inner do/while loops (fgets(s,...) for the code,
// atoi(s), then fgets(v,...) for the value). Returns false at EOF or on
// a "0" group code (the next entity's start, same terminator the
// original uses via `if (k == 0) break;`).
bool nextPair(FILE* fp, int& code, char value[256])
{
  char s[256];
  if (fgets(s, 256, fp) == nullptr)
    return false;
  code = atoi(s);
  if (code == 0)
    return false;
  if (fgets(value, 256, fp) == nullptr)
    return false;
  return true;
}

void addArcFromBulge(FemmProblem& problem, int n0, int n1, double angleDeg)
{
  FemmArcSegment asegm;
  asegm.n0 = n0;
  asegm.n1 = n1;
  asegm.arcLength = angleDeg;
  asegm.maxSideLength = 5.0;
  asegm.mySideLength = 5.0;
  problem.arcSegments.push_back(asegm);
}

// Splits a bulge-factor polyline edge (from node index `nA` to `nB`, in
// original edge-traversal order) into one or two arcs -- same
// n0/n1-by-sign-of-angle convention as femm/MOVECOPY.CPP's repeated
// inline logic (LWPOLYLINE, POLYLINE/VERTEX, and each of their
// closing-segment cases all duplicate this same pattern in the original;
// factored into one helper here instead of four copies). Simplified
// relative to the original's own node bookkeeping: the original inserts
// the midpoint node at a specific array position (CArray::InsertAt),
// which shifts every later node's index and forces it to juggle n0/n1
// afterward to compensate -- this always appends the midpoint at the end
// instead, since nothing here depends on node array order, so no
// juggling is needed; the two resulting half-arcs are simply n0->mid and
// mid->n1, in the same n0->n1 rotational sense as the original.
void addBulgeArc(FemmProblem& problem, int nA, int nB, double angleDeg, int currentGroup)
{
  int n0, n1;
  double angle = angleDeg;
  if (angle > 0) {
    n0 = nA;
    n1 = nB;
  } else {
    n0 = nB;
    n1 = nA;
    angle = std::fabs(angle);
  }

  if (angle > 180.0) {
    angle /= 2.0;
    Complex p0(problem.nodes[n0].x, problem.nodes[n0].y);
    Complex p1(problem.nodes[n1].x, problem.nodes[n1].y);
    Complex p2 = (p0 + p1) / 2.0 - Complex(0, 1) * (1.0 - std::cos(angle * M_PI / 180.0)) * (p1 - p0) / (2.0 * std::sin(angle * M_PI / 180.0));

    FemmNode mid;
    mid.x = p2.real();
    mid.y = p2.imag();
    mid.inGroup = currentGroup;
    int j = problem.nodes.size();
    problem.nodes.push_back(mid);

    addArcFromBulge(problem, n0, j, angle);
    problem.arcSegments.back().inGroup = currentGroup;
    addArcFromBulge(problem, j, n1, angle);
    problem.arcSegments.back().inGroup = currentGroup;
    return;
  }

  addArcFromBulge(problem, n0, n1, angle);
  problem.arcSegments.back().inGroup = currentGroup;
}

} // namespace

bool DxfIO::parseDxf(const QString& path, FemmProblem& parsed, double& suggestedTolerance, QString& errorMessage)
{
  FILE* fp = fopen(path.toLocal8Bit().constData(), "rt");
  if (!fp) {
    errorMessage = QStringLiteral("Could not open \"%1\" for reading.").arg(path);
    return false;
  }

  parsed = FemmProblem();
  g_layerList.clear();

  char s[256];
  // femm/MOVECOPY.CPP's PolylineFlag is a tri-state int (-1 = POLYLINE
  // just started, no VERTEX seen yet; FALSE = not in a POLYLINE; TRUE =
  // past the first VERTEX, each new one gets connected to the previous).
  enum class PolylineState { None,
    JustStarted,
    InProgress };
  PolylineState polylineState = PolylineState::None;
  int currentGroup = 0;
  double angle = 0.0;
  bool polyLineClosed = false;
  int firstPoint = -1;

  while (fgets(s, 256, fp) != nullptr) {
    if (strncmp(s, "LAYER", 5) == 0) {
      int code;
      char val[256];
      while (nextPair(fp, code, val)) {
        if (code == 2) {
          char name[256];
          sscanf(val, "%s", name);
          g_layerList.push_back(QString::fromLocal8Bit(name));
        }
      }
    }

    if (strncmp(s, "POINT", 5) == 0) {
      FemmNode n;
      int xx = 0;
      int code;
      char val[256];
      while (nextPair(fp, code, val)) {
        if (code == 10) {
          n.x = atof(val);
          xx |= 1;
        } else if (code == 20) {
          n.y = atof(val);
          xx |= 2;
        } else if (code == 8) {
          char name[256];
          sscanf(val, "%s", name);
          n.inGroup = layerToGroup(name);
        }
      }
      if (xx == 3)
        parsed.nodes.push_back(n);
    }

    if (strncmp(s, "LWPOLYLINE", 10) == 0) {
      int segs = 0;
      polyLineClosed = false;
      firstPoint = -1;
      angle = 0.0;
      currentGroup = 0;
      int xx = 0;
      FemmNode n;
      int code;
      char val[256];
      while (nextPair(fp, code, val)) {
        if (code == 8) {
          char name[256];
          sscanf(val, "%s", name);
          currentGroup = layerToGroup(name);
        } else if (code == 10) {
          n.x = atof(val);
          xx |= 1;
        } else if (code == 20) {
          n.y = atof(val);
          xx |= 2;
        } else if (code == 42) {
          angle = 720.0 * std::atan(atof(val)) / M_PI;
          angle = qBound(-360.0, angle, 360.0);
        } else if (code == 70) {
          polyLineClosed = true;
        }
        if (xx == 3) {
          n.inGroup = currentGroup;
          int j = parsed.nodes.size();
          parsed.nodes.push_back(n);
          xx = 0;
          if (segs == 0) {
            firstPoint = j;
          } else {
            if (angle == 0.0) {
              FemmSegment seg;
              seg.n0 = j;
              seg.n1 = j - 1;
              seg.inGroup = currentGroup;
              parsed.segments.push_back(seg);
            } else {
              addBulgeArc(parsed, j - 1, j, angle, currentGroup);
              angle = 0.0;
            }
          }
          segs++;
        }
      }
      if (polyLineClosed && firstPoint >= 0 && segs > 0) {
        int j = parsed.nodes.size() - 1;
        if (angle == 0.0) {
          FemmSegment seg;
          seg.n0 = j;
          seg.n1 = firstPoint;
          seg.inGroup = currentGroup;
          parsed.segments.push_back(seg);
        } else {
          addBulgeArc(parsed, j, firstPoint, angle, currentGroup);
        }
      }
    }

    if (strncmp(s, "POLYLINE", 8) == 0) {
      polylineState = PolylineState::JustStarted;
      firstPoint = -1;
      polyLineClosed = false;
      int code;
      char val[256];
      while (nextPair(fp, code, val)) {
        if (code == 70 && atoi(val) == 1)
          polyLineClosed = true;
      }
    }

    if (strncmp(s, "SEQEND", 6) == 0) {
      int j = parsed.nodes.size() - 1;
      if (polyLineClosed && firstPoint >= 0) {
        if (angle == 0.0) {
          FemmSegment seg;
          seg.n0 = j;
          seg.n1 = firstPoint;
          parsed.segments.push_back(seg);
        } else {
          addBulgeArc(parsed, j, firstPoint, angle, currentGroup);
        }
      }
      polyLineClosed = false;
      angle = 0.0;
      polylineState = PolylineState::None;
    }

    if (strncmp(s, "VERTEX", 6) == 0) {
      FemmNode n;
      int xx = 0;
      double nextAngle = 0.0;
      int code;
      char val[256];
      while (nextPair(fp, code, val)) {
        if (code == 8) {
          char name[256];
          sscanf(val, "%s", name);
          currentGroup = layerToGroup(name);
        } else if (code == 10) {
          n.x = atof(val);
          xx |= 1;
        } else if (code == 20) {
          n.y = atof(val);
          xx |= 2;
        } else if (code == 42) {
          nextAngle = 720.0 * std::atan(atof(val)) / M_PI;
          nextAngle = qBound(-360.0, nextAngle, 360.0);
        }
      }
      if (xx == 3) {
        n.inGroup = currentGroup;
        parsed.nodes.push_back(n);
        if (angle != 0.0) {
          int j = parsed.nodes.size();
          addBulgeArc(parsed, j - 2, j - 1, angle, currentGroup);
        } else {
          if (polylineState == PolylineState::InProgress) {
            int j = parsed.nodes.size();
            FemmSegment seg;
            seg.n0 = j - 2;
            seg.n1 = j - 1;
            seg.inGroup = currentGroup;
            parsed.segments.push_back(seg);
          }
          if (polylineState == PolylineState::JustStarted) {
            firstPoint = parsed.nodes.size() - 1;
            polylineState = PolylineState::InProgress;
          }
        }
      }
      angle = nextAngle;
    }

    if (strncmp(s, "LINE", 4) == 0) {
      FemmNode n0, n1;
      int xx = 0;
      int group = 0;
      int code;
      char val[256];
      while (nextPair(fp, code, val)) {
        if (code == 8) {
          char name[256];
          sscanf(val, "%s", name);
          group = layerToGroup(name);
        } else if (code == 10) {
          n0.x = atof(val);
          xx |= 1;
        } else if (code == 20) {
          n0.y = atof(val);
          xx |= 2;
        } else if (code == 11) {
          n1.x = atof(val);
          xx |= 4;
        } else if (code == 21) {
          n1.y = atof(val);
          xx |= 8;
        }
      }
      if (xx == 15) {
        n0.inGroup = n1.inGroup = group;
        int j = parsed.nodes.size();
        parsed.nodes.push_back(n0);
        parsed.nodes.push_back(n1);
        FemmSegment seg;
        seg.n0 = j;
        seg.n1 = j + 1;
        seg.inGroup = group;
        parsed.segments.push_back(seg);
      }
    }

    // catch ARCALIGNEDTEXT, which derails the ARC code -- femm/MOVECOPY.CPP's own workaround.
    if (strncmp(s, "ARCA", 4) == 0)
      s[0] = 0;

    if (strncmp(s, "ARC", 3) == 0) {
      Complex c, p, q;
      double R = 0, a0 = 0, a1 = 0;
      int xx = 0;
      int group = 0;
      int code;
      char val[256];
      while (nextPair(fp, code, val)) {
        if (code == 8) {
          char name[256];
          sscanf(val, "%s", name);
          group = layerToGroup(name);
        } else if (code == 10) {
          c = Complex(atof(val), c.imag());
          xx |= 1;
        } else if (code == 20) {
          c = Complex(c.real(), atof(val));
          xx |= 2;
        } else if (code == 40) {
          R = atof(val);
          xx |= 4;
        } else if (code == 50) {
          a0 = atof(val);
          xx |= 8;
        } else if (code == 51) {
          a1 = atof(val);
          xx |= 16;
        }
      }
      if (xx == 31) {
        if (a1 < a0)
          a1 += 360.0;
        FemmNode n0, n1;
        n0.inGroup = n1.inGroup = group;
        if ((a1 - a0) <= 180.0) {
          p = R * std::exp(Complex(0, 1) * M_PI * a0 / 180.0) + c;
          q = R * std::exp(Complex(0, 1) * M_PI * a1 / 180.0) + c;
          n0.x = p.real();
          n1.x = q.real();
          n0.y = p.imag();
          n1.y = q.imag();
          int j = parsed.nodes.size();
          parsed.nodes.push_back(n0);
          parsed.nodes.push_back(n1);
          FemmArcSegment asegm;
          asegm.n0 = j;
          asegm.n1 = j + 1;
          asegm.maxSideLength = 5.0;
          asegm.mySideLength = 5.0;
          asegm.arcLength = a1 - a0;
          asegm.inGroup = group;
          parsed.arcSegments.push_back(asegm);
        } else {
          p = R * std::exp(Complex(0, 1) * M_PI * a0 / 180.0) + c;
          q = R * std::exp(Complex(0, 1) * M_PI * (a1 + a0) / 360.0) + c;
          n0.x = p.real();
          n1.x = q.real();
          n0.y = p.imag();
          n1.y = q.imag();
          int j = parsed.nodes.size();
          parsed.nodes.push_back(n0);
          parsed.nodes.push_back(n1);
          FemmArcSegment asegm1;
          asegm1.n0 = j;
          asegm1.n1 = j + 1;
          asegm1.maxSideLength = 5.0;
          asegm1.mySideLength = 5.0;
          asegm1.arcLength = (a1 - a0) / 2.0;
          asegm1.inGroup = group;
          parsed.arcSegments.push_back(asegm1);

          p = q;
          q = R * std::exp(Complex(0, 1) * M_PI * a1 / 180.0) + c;
          FemmNode n2;
          n2.inGroup = group;
          n2.x = q.real();
          n2.y = q.imag();
          int j2 = parsed.nodes.size();
          parsed.nodes.push_back(n2);
          FemmArcSegment asegm2;
          asegm2.n0 = j + 1;
          asegm2.n1 = j2;
          asegm2.maxSideLength = 5.0;
          asegm2.mySideLength = 5.0;
          asegm2.arcLength = (a1 - a0) / 2.0;
          asegm2.inGroup = group;
          parsed.arcSegments.push_back(asegm2);
        }
      }
    }

    if (strncmp(s, "CIRCLE", 6) == 0) {
      Complex c;
      double R = 0;
      int xx = 0;
      int group = 0;
      int code;
      char val[256];
      while (nextPair(fp, code, val)) {
        if (code == 8) {
          char name[256];
          sscanf(val, "%s", name);
          group = layerToGroup(name);
        } else if (code == 10) {
          c = Complex(atof(val), c.imag());
          xx |= 1;
        } else if (code == 20) {
          c = Complex(c.real(), atof(val));
          xx |= 2;
        } else if (code == 40) {
          R = atof(val);
          xx |= 4;
        }
      }
      if (xx == 7) {
        FemmNode n0, n1;
        n0.inGroup = n1.inGroup = group;
        n0.x = c.real() + R;
        n1.x = c.real() - R;
        n0.y = c.imag();
        n1.y = c.imag();
        int j = parsed.nodes.size();
        parsed.nodes.push_back(n0);
        parsed.nodes.push_back(n1);
        FemmArcSegment a1;
        a1.n0 = j;
        a1.n1 = j + 1;
        a1.maxSideLength = 5.0;
        a1.mySideLength = 5.0;
        a1.arcLength = 180.0;
        a1.inGroup = group;
        parsed.arcSegments.push_back(a1);
        FemmArcSegment a2;
        a2.n0 = j + 1;
        a2.n1 = j;
        a2.maxSideLength = 5.0;
        a2.mySideLength = 5.0;
        a2.arcLength = 180.0;
        a2.inGroup = group;
        parsed.arcSegments.push_back(a2);
      }
    }
  }

  fclose(fp);

  if (parsed.nodes.isEmpty()) {
    errorMessage = "No recognized geometry (POINT/LINE/ARC/CIRCLE/LWPOLYLINE/POLYLINE) found in the file.";
    return false;
  }

  double x0 = parsed.nodes[0].x, x1 = x0, y0 = parsed.nodes[0].y, y1 = y0;
  for (const FemmNode& n : parsed.nodes) {
    x0 = std::min(x0, n.x);
    x1 = std::max(x1, n.x);
    y0 = std::min(y0, n.y);
    y1 = std::max(y1, n.y);
  }
  double R = std::hypot(x1 - x0, y1 - y0) * 1e-4;
  if (R > 0)
    suggestedTolerance = std::floor(R / std::pow(10.0, std::floor(std::log10(R)))) * std::pow(10.0, std::floor(std::log10(R)));
  else
    suggestedTolerance = 1e-8;

  return true;
}

void DxfIO::mergeCoincidentNodes(FemmProblem& problem, double tolerance)
{
  if (tolerance <= 0)
    return;
  int n = problem.nodes.size();
  QVector<int> remap(n);
  for (int i = 0; i < n; i++)
    remap[i] = i;

  // O(n^2) coincident-node search -- acceptable for DXF imports (typically
  // thousands, not millions, of nodes); see DxfIO.h's header comment for
  // why this doesn't also resolve non-coincident crossings.
  for (int i = 0; i < n; i++) {
    if (remap[i] != i)
      continue;
    for (int j = i + 1; j < n; j++) {
      if (remap[j] != j)
        continue;
      double dx = problem.nodes[i].x - problem.nodes[j].x;
      double dy = problem.nodes[i].y - problem.nodes[j].y;
      if (std::hypot(dx, dy) < tolerance)
        remap[j] = i;
    }
  }

  // Compact: assign each surviving node (remap[i] == i) a new dense index.
  QVector<int> newIndex(n, -1);
  QVector<FemmNode> newNodes;
  for (int i = 0; i < n; i++) {
    if (remap[i] == i) {
      newIndex[i] = newNodes.size();
      newNodes.push_back(problem.nodes[i]);
    }
  }
  auto resolve = [&](int oldIdx) {
    return newIndex[remap[oldIdx]];
  };

  QVector<FemmSegment> newSegments;
  for (FemmSegment seg : problem.segments) {
    seg.n0 = resolve(seg.n0);
    seg.n1 = resolve(seg.n1);
    if (seg.n0 != seg.n1)
      newSegments.push_back(seg);
  }
  QVector<FemmArcSegment> newArcs;
  for (FemmArcSegment arc : problem.arcSegments) {
    arc.n0 = resolve(arc.n0);
    arc.n1 = resolve(arc.n1);
    if (arc.n0 != arc.n1)
      newArcs.push_back(arc);
  }

  problem.nodes = newNodes;
  problem.segments = newSegments;
  problem.arcSegments = newArcs;
}

bool DxfIO::exportDxf(const QString& path, const FemmProblem& problem, QString& errorMessage)
{
  FILE* fp = fopen(path.toLocal8Bit().constData(), "wt");
  if (!fp) {
    errorMessage = QStringLiteral("Could not open \"%1\" for writing.").arg(path);
    return false;
  }

  double extMinX = 0, extMinY = 0, extMaxX = 1, extMaxY = 1;
  if (problem.nodes.size() >= 2) {
    extMinX = extMaxX = problem.nodes[0].x;
    extMinY = extMaxY = problem.nodes[0].y;
    for (const FemmNode& n : problem.nodes) {
      extMinX = std::min(extMinX, n.x);
      extMaxX = std::max(extMaxX, n.x);
      extMinY = std::min(extMinY, n.y);
      extMaxY = std::max(extMaxY, n.y);
    }
  }
  double margin = 0.025 * std::hypot(extMaxX - extMinX, extMaxY - extMinY);

  fprintf(fp, "  0\nSECTION\n  2\nHEADER\n  9\n");
  fprintf(fp, "$INSBASE\n 10\n0.0000\n 20\n0.0000\n  9\n");
  fprintf(fp, "$EXTMIN\n 10\n%.17g\n 20\n%.17g\n  9\n", extMinX - margin, extMinY - margin);
  fprintf(fp, "$EXTMAX\n 10\n%.17g\n 20\n%.17g\n  9\n", extMaxX + margin, extMaxY + margin);
  fprintf(fp, "$LIMMIN\n 10\n%.17g\n 20\n%.17g\n  9\n", extMinX - margin, extMinY - margin);
  fprintf(fp, "$LIMMAX\n 10\n%.17g\n 20\n%.17g\n  9\n", extMaxX + margin, extMaxY + margin);
  fprintf(fp, "$TEXTSTYLE\n  7\nSTANDARD\n  9\n$CLAYER\n");
  fprintf(fp, "  8\ndefault\n  0\nENDSEC\n  0\n");
  fprintf(fp, "SECTION\n  2\nTABLES\n  0\n");
  fprintf(fp, "TABLE\n  2\nLTYPE\n 70\n4948253\n  0\n");
  fprintf(fp, "LTYPE\n  2\nCONTINUOUS\n 70\n    64\n  3\n");
  fprintf(fp, "Solid line\n 72\n    65\n 73\n     0\n 40\n0.0\n  0\nENDTAB\n  0\n");
  fprintf(fp, "TABLE\n  2\nLAYER\n 70\n     5\n  0\n");
  fprintf(fp, "LAYER\n  2\ndefault\n 70\n    64\n 62\n     7\n  6\n");
  fprintf(fp, "CONTINUOUS\n  0\n");

  QVector<int> layers;
  for (const FemmSegment& s : problem.segments)
    if (s.inGroup != 0 && !layers.contains(s.inGroup))
      layers.push_back(s.inGroup);
  for (const FemmArcSegment& a : problem.arcSegments)
    if (a.inGroup != 0 && !layers.contains(a.inGroup))
      layers.push_back(a.inGroup);

  for (int g : layers) {
    fprintf(fp, "LAYER\n  2\nlayer%d\n 70\n    64\n 62\n     7\n  6\n", g);
    fprintf(fp, "CONTINUOUS\n  0\n");
  }

  fprintf(fp, "ENDTAB\n  0\nTABLE\n  2\n");
  fprintf(fp, "STYLE\n 70\n     1\n  0\nSTYLE\n  2\nSTANDARD\n 70\n");
  fprintf(fp, "     0\n 40\n0.0\n 41\n1.0\n 50\n0.0\n 71\n     0\n");
  fprintf(fp, " 42\n0.2\n  3\ntxt\n  4\n\n  0\nENDTAB\n  0\n");
  fprintf(fp, "TABLE\n  2\nVIEW\n 70\n     0\n  0\nENDTAB\n  0\n");
  fprintf(fp, "ENDSEC\n  0\nSECTION\n  2\nBLOCKS\n  0\nENDSEC\n");
  fprintf(fp, "  0\nSECTION\n  2\nENTITIES\n  0\n");

  for (const FemmSegment& s : problem.segments) {
    if (s.n0 < 0 || s.n0 >= problem.nodes.size() || s.n1 < 0 || s.n1 >= problem.nodes.size())
      continue;
    const FemmNode& a = problem.nodes[s.n0];
    const FemmNode& b = problem.nodes[s.n1];
    QByteArray layer = s.inGroup == 0 ? QByteArray("default") : QByteArray("layer") + QByteArray::number(s.inGroup);
    fprintf(fp, "LINE\n  8\n%s\n 10\n%.17g\n 20\n%.17g\n 30\n0.0\n 11\n%.17g\n 21\n%.17g\n 31\n0.0\n  0\n",
        layer.constData(), a.x, a.y, b.x, b.y);
  }

  for (const FemmArcSegment& arc : problem.arcSegments) {
    if (arc.n0 < 0 || arc.n0 >= problem.nodes.size() || arc.n1 < 0 || arc.n1 >= problem.nodes.size())
      continue;
    const FemmNode& a = problem.nodes[arc.n0];
    const FemmNode& b = problem.nodes[arc.n1];
    // Circle center/radius from the two endpoints + included angle
    // (femm/MOVECOPY.CPP's GetCircle, ported inline -- solves for the
    // center by rotating the chord's perpendicular bisector).
    Complex p0(a.x, a.y), p1(b.x, b.y);
    double halfAngle = arc.arcLength * M_PI / 360.0;
    Complex mid = (p0 + p1) / 2.0;
    Complex chord = p1 - p0;
    double chordLen = std::abs(chord);
    double R = (chordLen / 2.0) / std::sin(halfAngle);
    double h = R * std::cos(halfAngle);
    Complex perp = Complex(0, 1) * (chord / (chordLen != 0 ? chordLen : 1.0));
    Complex c = mid - perp * h;

    double x0 = std::arg(p0 - c) * 180.0 / M_PI;
    double x1 = std::arg(p1 - c) * 180.0 / M_PI;
    if (x0 < 0)
      x0 += 360.0;
    if (x1 < 0)
      x1 += 360.0;
    QByteArray layer = arc.inGroup == 0 ? QByteArray("default") : QByteArray("layer") + QByteArray::number(arc.inGroup);
    fprintf(fp, "ARC\n  8\n%s\n 10\n%.17g\n 20\n%.17g\n 30\n0.0\n 40\n%.17g\n 50\n%.17g\n 51\n%.17g\n  0\n",
        layer.constData(), c.real(), c.imag(), R, x0, x1);
  }

  fprintf(fp, "ENDSEC\n  0\nEOF\n");
  fclose(fp);
  return true;
}
