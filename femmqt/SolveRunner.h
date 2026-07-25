#pragma once

#include <QString>

struct FemmProblem;

// Drives the same external two-stage solve pipeline the existing MFC GUI
// uses (femm/FemmeView.cpp:2743-2825): triangle.exe (mesh generation, via
// MeshBuilder's .poly/.pbc) then fkn.exe (the actual solve), both found
// next to femmqt.exe itself (same directory all these executables already
// ship flat in, see script.nsi). Solves synchronously (blocks the calling
// thread until done) -- acceptable for this phase; a future pass could
// switch to QProcess's async signals for a non-blocking UI during long
// solves.
namespace SolveRunner {

// `femPath` must already be saved to disk (this reimplements OnWritePoly,
// which reads the saved .fem's own path to derive .poly/.pbc/.ans
// sibling paths -- it does not take the in-memory FemmProblem's fields
// as ground truth for anything except the mesh-generation geometry
// itself). Returns true on success; on failure, false with a short
// user-presentable reason in `errorMessage`.
bool solve(const FemmProblem& problem, const QString& femPath, QString& errorMessage);

// Just the triangle.exe meshing stage, for Mesh > Create Mesh (a separate
// command from Solve in the classic GUI -- femm/FemmeView.cpp's
// OnMakeMesh/LoadMesh -- that meshes without invoking fkn.exe, so the
// mesh can be inspected/refined before committing to a full solve).
// Leaves rootPath.node/.edge/.ele on disk for a caller to read (see
// MeshOverlay::load).
bool mesh(const FemmProblem& problem, const QString& femPath, QString& errorMessage);

}
