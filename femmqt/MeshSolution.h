#pragma once

#include <QVector>

// Solved-mesh data read from a .ans file (femm/FemmviewDoc.cpp's
// [Solution] section) -- kept as flat arrays of plain POD structs, not
// QGraphicsItems, matching the plan's decision that per-mesh-triangle
// graphics items don't scale to the millions-of-elements meshes this app
// is meant to handle. SolutionView paints directly from these.
struct MeshSolutionNode {
  double x = 0, y = 0;
  double Are = 0, Aim = 0;
};

struct MeshSolutionElement {
  int p0 = 0, p1 = 0, p2 = 0;
  int lbl = 0; // index into FemmProblem::blockLabels
  double B1re = 0, B1im = 0, B2re = 0, B2im = 0; // flux density, precomputed (see AnsFileIO::computeElementFields)
  double ctrX = 0, ctrY = 0; // centroid, precomputed
};

struct MeshSolution {
  QVector<MeshSolutionNode> nodes;
  QVector<MeshSolutionElement> elements;

  // |B| = sqrt(|B1|^2+|B2|^2) extremes across all elements, precomputed
  // once for the density plot's legend range.
  double bMagMin = 0, bMagMax = 0;
};
