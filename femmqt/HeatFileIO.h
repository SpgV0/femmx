#pragma once

#include <QString>

struct FemmProblem;

// .feh file I/O -- the heat-flow counterpart to FemmFileIO, reimplementing
// the exact text format written/read by femm/HDRAWDOC.CPP's
// ChdrawDoc::OnSaveDocument/OnOpenDocument, independently (not shared code
// with femm/), so files stay fully interchangeable between femmx.exe and
// femmqt.exe. Tag names/formats confirmed directly against HDRAWDOC.CPP,
// not re-derived -- see FemmProblem.h's Round 6 comment for the thermal
// struct fields this reads/writes.
//
// Reads/writes only the thermal-specific sections of a FemmProblem
// (thermalPointProps/thermalBoundaryProps/thermalMaterialProps/
// thermalConductorProps, plus each node/segment/arc/block-label's thermal
// index) plus the shared top-level scalar fields (precision/minAngle/
// depth/lengthUnits/problemType/coordsPolar/extZo,Ro,Ri/gpuAccel/comment)
// -- geometry (nodes/segments/arcs/block-label positions) is written too,
// since .feh is a standalone, independently-openable file exactly like
// .fem is, but the geometry itself is expected to already match what a
// paired .fem (if any) describes.
namespace HeatFileIO {

// Returns true and fills `problem` on success -- geometry/thermal fields
// only; magnetics fields are left at FemmProblem's defaults. On failure,
// returns false and fills `errorMessage` with a short, user-presentable
// reason.
bool readFeh(const QString& path, FemmProblem& problem, QString& errorMessage);

bool writeFeh(const QString& path, const FemmProblem& problem, QString& errorMessage);

} // namespace HeatFileIO
