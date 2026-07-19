#include "FemmProblemEdit.h"

#include "FemmProblem.h"

#include <QHash>

#include <cmath>

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
