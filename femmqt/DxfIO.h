#pragma once

#include "FemmProblem.h"

#include <QString>

// AutoCAD DXF import/export -- Qt port of femm/MOVECOPY.CPP's
// CFemmeDoc::ReadDXF/WriteDXF (the ASCII DXF group-code format, POINT/
// LINE/ARC/CIRCLE/LWPOLYLINE/POLYLINE+VERTEX entities, bulge-factor arc
// conversion for polylines -- transcribed directly from that file rather
// than reinvented).
namespace DxfIO {

// Parses `path` into a fresh, standalone FemmProblem (nodes/segments/arcs
// only -- DXF has no concept of FEMM's block labels/materials, matching
// classic FEMM's own ReadDXF). Layers become group numbers, same as
// classic FEMM. Also computes the same suggested merge tolerance classic
// FEMM's CDXFImport dialog auto-fills (1e-4 of the bounding box diagonal,
// rounded to 1 significant figure) -- the caller decides what to do with
// it (MainWindow prompts for it via QInputDialog rather than reproducing
// the classic dialog's own UI).
bool parseDxf(const QString& path, FemmProblem& parsed, double& suggestedTolerance, QString& errorMessage);

// Merges nodes within `tolerance` of each other and remaps/drops segments
// and arcs accordingly. Simplified relative to classic FEMM's own
// FancyEnforcePSLG: that also splits segments/arcs at newly-discovered
// intersections so the result is a valid planar straight-line graph even
// when the source DXF has crossing entities that don't share endpoints --
// genuinely complex computational geometry (arbitrary segment-segment/
// segment-arc/arc-arc intersection), out of scope here. This handles the
// common case (coincident endpoints from independently-drawn entities
// that were meant to connect) but a DXF with genuinely crossing,
// non-coincident geometry will import with those crossings unresolved.
void mergeCoincidentNodes(FemmProblem& problem, double tolerance);

// Writes `problem`'s nodes/segments/arcs as LINE/ARC entities (matching
// classic FEMM's own WriteDXF -- block labels aren't DXF entities, so
// they're not exported, same as classic).
bool exportDxf(const QString& path, const FemmProblem& problem, QString& errorMessage);

} // namespace DxfIO
