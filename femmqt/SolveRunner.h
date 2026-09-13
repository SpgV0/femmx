#pragma once

#include <QString>

struct FemmProblem;
enum class FemmProblemKind;

// Drives the same external two-stage solve pipeline the existing MFC GUI
// uses (femm/FemmeView.cpp:2743-2825): triangle.exe (mesh generation, via
// MeshBuilder's .poly/.pbc) then fkn.exe (the actual solve), both found
// next to femmqt.exe itself (same directory all these executables already
// ship flat in, see script.nsi). Solves synchronously (blocks the calling
// thread until done) -- acceptable for this phase; a future pass could
// switch to QProcess's async signals for a non-blocking UI during long
// solves.
namespace SolveRunner {

// `filePath` must already be saved to disk as a .fem -- this reimplements
// OnWritePoly, which reads the saved file's own path to derive
// .poly/.pbc/.ans sibling paths -- it does not take the in-memory
// FemmProblem's fields as ground truth for anything except the
// mesh-generation geometry itself). Returns true on success; on failure,
// false with a short user-presentable reason in `errorMessage`.
bool solve(const FemmProblem& problem, const QString& filePath, QString& errorMessage);

// Just the triangle.exe meshing stage, for Mesh > Create Mesh (a separate
// command from Solve in the classic GUI -- femm/FemmeView.cpp's
// OnMakeMesh/LoadMesh -- that meshes without invoking fkn.exe, so the mesh
// can be inspected/refined before committing to a full solve). Leaves
// rootPath.node/.edge/.ele on disk for a caller to read (see
// MeshOverlay::load).
bool mesh(const FemmProblem& problem, const QString& filePath, QString& errorMessage);

// What a solver's non-zero exit code means, for this kind's solver.
//
// Published rather than private because it is the one piece of #82 worth
// testing on its own: the four solvers do NOT agree on their codes.
// hsolv's are shifted by one from 3 upward and it reuses 7 for two
// different failures, so using fkn's table for heat flow mislabels every
// failure it can have.
QString exitMessage(FemmProblemKind kind, int code);

}
