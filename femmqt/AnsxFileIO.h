#pragma once

#include <QString>

struct MeshSolution;

// .ansx: a pure-binary cache of a solved .ans file's mesh, designed so
// loading it is a bulk fread() into flat POD arrays with zero per-record
// parsing or floating-point work -- B1/B2/centroid are precomputed once
// by the converter (see AnsFileIO::readAns, which already computes them
// for the plain-.ans path; the converter just serializes that same
// result) rather than recomputed on every load, unlike .ans's text
// format which requires re-parsing and re-deriving them every time.
//
// Deliberately NOT a serialization of the full FemmProblem (materials/
// boundary conditions/original geometry) -- SolutionWindow only ever
// needs the mesh (MeshSolution) to render the density plot, and that
// header/property data is cheap to re-read from the small, non-mesh part
// of the source .ans on the rare occasions it's needed. Keeping .ansx
// scoped to just the expensive part (the mesh) keeps the format simple
// and keeps it obviously always regenerable from its .ans, matching its
// role as a pure performance cache, not a second source of truth.
namespace AnsxFileIO {

// True if "<ansPath minus extension>.ansx" exists and its recorded
// source size/mtime still match `ansPath` on disk.
bool isUpToDate(const QString& ansxPath, const QString& ansPath);

bool writeAnsx(const QString& ansxPath, const QString& sourceAnsPath,
    int coordSystem, int lengthUnits, double frequency,
    const MeshSolution& solution, QString& errorMessage);

// coordSystemOut, if non-null, receives the header's stored coordinate
// system (0 = planar, 1 = axisymmetric) -- lets a caller that only needs
// that one field (e.g. to label a value's unit) avoid a second file read
// just to get it, per this header's own note above about re-reading the
// source .ans being the normal way to get FemmProblem data back.
bool readAnsx(const QString& ansxPath, MeshSolution& solution, QString& errorMessage, int* coordSystemOut = nullptr);

}
