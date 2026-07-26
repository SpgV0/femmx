#pragma once

#include <QString>

struct FemmProblem;

// .fem file I/O -- reimplements the exact text format written by
// femm/FemmeDoc.cpp::OnSaveDocument (and read by its OnOpenDocument,
// fkn/femmedoccore.cpp, triangle.exe's caller, etc.), independently, so
// files stay fully interchangeable between femmx.exe and femmqt.exe. Not
// shared code with femm/ -- see FemmProblem.h's header comment for why.
namespace FemmFileIO {

// Returns true and fills `problem` on success. On failure, returns false
// and fills `errorMessage` with a short, user-presentable reason.
bool readFem(const QString& path, FemmProblem& problem, QString& errorMessage);

bool writeFem(const QString& path, const FemmProblem& problem, QString& errorMessage);

} // namespace FemmFileIO
