#pragma once

#include <QString>

struct FemmProblem;
struct MeshSolution;

// Reads a solved .ans file: the geometry/property header (byte-for-byte
// the same format as .fem, see FemmFileIO's header comment) plus the
// appended [Solution] mesh section (femm/FemmviewDoc.cpp's OnOpenDocument,
// ~line 985 onward). Independent reimplementation, not shared with
// femm/FemmviewDoc.cpp -- matches this codebase's existing per-consumer-
// parser convention.
namespace AnsFileIO {

bool readAns(const QString& path, FemmProblem& problem, MeshSolution& solution, QString& errorMessage);

}
