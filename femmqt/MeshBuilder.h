#pragma once

#include <QString>

struct FemmProblem;

// Reimplements CFemmeDoc::OnWritePoly (femm/writepoly.cpp:91-377): given a
// solvable FemmProblem, discretizes its segments/arcs into a Triangle-format
// PSLG and writes "<rootPath>.poly" + a trivial "<rootPath>.pbc", ready for
// triangle.exe. Independent reimplementation (pure geometry/text work, not
// shared with femm/fkn's own copy), matching the codebase's existing
// per-consumer-parser convention -- see FemmFileIO's header comment.
//
// Periodic/antiperiodic boundary conditions (femm/writepoly.cpp's
// FunnyOnWritePoly path) are out of scope for this phase -- callers should
// check FemmProblem for periodic BdryFormat values (4-7) themselves and
// decline to solve rather than call this.
namespace MeshBuilder {

bool writePolyAndPbc(const FemmProblem& problem, const QString& rootPath, QString& errorMessage);

}
