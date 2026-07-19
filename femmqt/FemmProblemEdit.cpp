#include "FemmProblemEdit.h"

#include "FemmProblem.h"

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
