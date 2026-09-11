#pragma once

// Which window a file opens in.
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-11 (issue
// #19). main() decided this twice -- once for --render-png and once for
// the GUI-switch handoff -- with the same two-way suffix comparison
// written out both times. Two copies of a routing rule is one copy too
// many: a third extension added to one and not the other would send the
// same file to different windows depending on which entry point opened
// it. It also could not be tested, being a local expression inside
// main(); it now can.
//
// The distinction matters because a solved mesh is not editable geometry:
// handing a .ans to the geometry editor would have it try to load a
// possibly-huge solved mesh as if it were a sketch.

#include <QString>

namespace FileRouting {

// True for a magnetics solution file (.ans, or its .ansx binary cache),
// which belongs in the Solution Viewer. Case-insensitive, matching the
// classic GUI's own file-association behaviour on Windows.
bool isSolutionFile(const QString& path);

} // namespace FileRouting
