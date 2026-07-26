#pragma once

#include "FemmProblem.h"

#include <QString>

struct FemmProblem;

// Drives the same external two-stage solve pipeline the existing MFC GUI
// uses (femm/FemmeView.cpp:2743-2825): triangle.exe (mesh generation, via
// MeshBuilder's .poly/.pbc) then fkn.exe/hsolv.exe (the actual solve),
// both found next to femmqt.exe itself (same directory all these
// executables already ship flat in, see script.nsi). Solves synchronously
// (blocks the calling thread until done) -- acceptable for this phase; a
// future pass could switch to QProcess's async signals for a non-blocking
// UI during long solves.
namespace SolveRunner {

// `filePath` must already be saved to disk as the file matching
// `physicsType` (.fem for Magnetics, .feh for HeatFlow -- this
// reimplements OnWritePoly, which reads the saved file's own path to
// derive .poly/.pbc/.ans-or-.anh sibling paths -- it does not take the
// in-memory FemmProblem's fields as ground truth for anything except the
// mesh-generation geometry itself). Returns true on success; on failure,
// false with a short user-presentable reason in `errorMessage`.
bool solve(const FemmProblem& problem, const QString& filePath, QString& errorMessage, FemmPhysicsType physicsType = FemmPhysicsType::Magnetics);

// Just the triangle.exe meshing stage, for Mesh > Create Mesh (a separate
// command from Solve in the classic GUI -- femm/FemmeView.cpp's
// OnMakeMesh/LoadMesh -- that meshes without invoking fkn.exe/hsolv.exe,
// so the mesh can be inspected/refined before committing to a full
// solve). Leaves rootPath.node/.edge/.ele on disk for a caller to read
// (see MeshOverlay::load) -- identical either way, since the mesh itself
// doesn't depend on which physics eventually solves on it (only which
// per-entity markers get encoded into it, see MeshBuilder's own comment).
bool mesh(const FemmProblem& problem, const QString& filePath, QString& errorMessage, FemmPhysicsType physicsType = FemmPhysicsType::Magnetics);

}
