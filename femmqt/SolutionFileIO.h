#pragma once

// SolutionFileIO.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #83).
//
// Reads the [Solution] section of any of the four solution formats --
// .ans, .res, .anh, .anc -- into one representation.
//
// A solution file IS its model file with a [Solution] section appended,
// so the half of the work that reads properties and geometry is
// ProblemFileIO's and is not repeated here. What this adds is the mesh
// and the solved potential.
//
// WHAT DIFFERS BETWEEN THE FOUR, and why a single reader is honest
// rather than a flattening:
//
//   * Element lines are identical everywhere: three node indices and a
//     block-label index.
//   * Node lines all begin with x and y and then carry that physics'
//     potential -- A for magnetics, V for electrostatics and current
//     flow, T for heat flow.
//   * Only magnetics varies its own column count: A has an imaginary
//     part only when the problem's frequency is non-zero. Reading a DC
//     .ans as though it were AC consumes the NEXT line's x as A_im and
//     shifts everything after it, which parses cleanly and is wrong.
//   * The three non-magnetics formats carry a trailing conductor index
//     per node; magnetics does not -- the same asymmetry as the model
//     formats.
//
// This reads the potential only. Deriving B, E, flux or current density
// from it is the plotting layer's job and is per-physics; see
// FILE_FORMATS.md's "Derived quantities" table.

#include <QString>
#include <QVector>

struct FemmProblem;
enum class FemmProblemKind;

// One solved mesh node: where it is, what the potential is there, and
// which conductor it belongs to.
//
// `potentialIm` is meaningful only for an AC magnetics or a current-flow
// solution; `conductor` only for the three non-magnetics formats. Both
// stay zero otherwise rather than being left undefined, so a reader that
// ignores them gets the right answer.
struct SolutionNode {
  double x = 0, y = 0;
  double potentialRe = 0;
  double potentialIm = 0;
  int conductor = 0;
};

struct SolutionElement {
  int p0 = 0, p1 = 0, p2 = 0;
  int label = 0; // index into FemmProblem::blockLabels
};

struct SolvedMesh {
  QVector<SolutionNode> nodes;
  QVector<SolutionElement> elements;
};

namespace SolutionFileIO {

// Reads `path`'s model half into `problem` and its [Solution] section
// into `mesh`. The kind comes from the extension.
//
// A file with no [Solution] section is a failure with a reason, not an
// empty mesh: that is what an unsolved model looks like, and the two
// should not be confused.
bool read(const QString& path, FemmProblem& problem, SolvedMesh& mesh,
    QString& errorMessage);

// The problem kind a solution extension implies -- .ans/.res/.anh/.anc.
// False for anything else, including a model extension.
bool kindForSolutionPath(const QString& path, FemmProblemKind& kindOut);

} // namespace SolutionFileIO
