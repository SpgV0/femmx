#pragma once

#include <QString>
#include <QVector>

// Raw pre-solve mesh geometry (triangle.exe's .node/.ele output) for the
// "Show Mesh" overlay in the geometry editor -- mirrors femm/FemmeDoc.cpp's
// LoadMesh(), but deliberately doesn't carry solved-field data the way
// MeshSolution.h does (there's nothing to solve for yet at this stage).
struct MeshOverlayNode {
  double x = 0, y = 0;
};

struct MeshOverlayElement {
  int p0 = 0, p1 = 0, p2 = 0;
};

struct MeshOverlay {
  QVector<MeshOverlayNode> nodes;
  QVector<MeshOverlayElement> elements;
};

namespace MeshOverlayIO {

// rootPath is the same "path without extension" SolveRunner::mesh() and
// MeshBuilder use -- reads rootPath+".node" and rootPath+".ele" (written by
// triangle.exe). Returns false (with a short reason) if either is missing.
bool load(const QString& rootPath, MeshOverlay& mesh, QString& errorMessage);

}
