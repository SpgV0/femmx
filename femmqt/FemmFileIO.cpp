#include "FemmFileIO.h"

#include "FemmProblem.h"
#include "ProblemFileIO.h"

// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #80).
//
// This file used to hold the whole .fem reader and writer. There are four
// model formats now, and they share a header, a geometry section and the
// entire section-dispatch loop -- so that machinery moved to
// ProblemFileIO, with the parts that genuinely differ in PropertyCodec.
//
// What remains is the magnetics-only entry point, kept because plenty of
// code and several tests call readFem/writeFem by name and magnetics is
// still what they mean. It is a thin forward now rather than a second
// implementation: two readers for one format is how the two halves drift
// apart, and this repo has already paid for that twice -- #77's arc
// centre, and .femx's two copies.

bool FemmFileIO::readFem(const QString& path, FemmProblem& problem, QString& errorMessage)
{
  // Forced to Magnetics rather than inferred from the extension: a
  // caller that asked for readFem has said which format it expects, and
  // silently reading a .fee here because someone renamed a file would
  // hand back a model with no properties at all.
  return ProblemFileIO::readAs(path, FemmProblemKind::Magnetics, problem, errorMessage);
}

bool FemmFileIO::writeFem(const QString& path, const FemmProblem& problem, QString& errorMessage)
{
  if (problem.kind != FemmProblemKind::Magnetics) {
    errorMessage = QStringLiteral(
        "writeFem() writes magnetics .fem files; this problem is not a "
        "magnetics problem. Use ProblemFileIO::write().");
    return false;
  }
  return ProblemFileIO::write(path, problem, errorMessage);
}
