#pragma once

// PropertyCodec.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #80).
//
// The half of a FEMM model file that differs between the four physics:
// [PointProps], [BdryProps], [BlockProps] and the source list
// ([CircuitProps] for magnetics, [ConductorProps] for the other three),
// plus the handful of header scalars that are not common to all four
// (<Frequency>, <ACSolver>, <PrevType> for magnetics, <Frequency> for
// current flow, <dT> and <PrevSoln> for heat flow).
//
// Everything else -- the common header scalars and the whole geometry
// section -- is identical across the four formats and is written once,
// in ProblemFileIO.
//
// Every tag below was taken from the corresponding classic writer
// (femm/FemmeDoc.cpp, beladrawDoc.cpp, hdrawDoc.cpp, cdrawDoc.cpp), not
// from memory or documentation. The tags are the contract with the
// solvers: a file whose tag is misspelled does not fail to load, it
// loads with that field silently at zero.

#include <QString>
#include <functional>

class QTextStream;
struct FemmProblem;

namespace PropertyCodec {

// Pulls the next line of the file, returning false at end of input. The
// property records are line-oriented and self-delimiting (<BeginBlock>
// ... <EndBlock>), so a codec consumes lines until it sees its own
// closing tag; the driver owns the stream and hands over this much.
using LineReader = std::function<bool(QString&)>;

// Reads the property section `tag` (without brackets) holding `count`
// records, into whichever list of `p` corresponds to p.kind.
//
// Returns false when `tag` is not a property section for this kind, so
// the driver can go on to try it as a header scalar or geometry.
bool readSection(FemmProblem& p, const QString& tag, int count, const LineReader& next);

// Reads a header scalar specific to p.kind. Returns false if `tag` is
// not one of them.
bool readKindScalar(FemmProblem& p, const QString& tag, const QString& value);

// Writes this kind's header extras. Called by the driver in the position
// the classic writer puts them.
void writeKindScalars(const FemmProblem& p, QTextStream& out);

// Writes all four property sections for p.kind, in the order the classic
// writer for that format uses.
void writeSections(const FemmProblem& p, QTextStream& out);

} // namespace PropertyCodec
