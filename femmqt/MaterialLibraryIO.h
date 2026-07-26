#pragma once

#include "FemmProblem.h"

#include <QString>
#include <QVector>

// Reads bin/matlib.dat (femm/fe_libdlg.cpp's shared magnetics material
// library) and bin/heatlib.dat (femm/hd_libdlg.cpp's shared heat-flow
// one) and merges them into ONE tree of unified FemmMaterialProp entries
// -- per direct user request: one materials library, and every entry
// shows both its magnetic and thermal properties (0/empty on whichever
// side its source file(s) didn't define -- see FemmMaterialProp's own
// comment). Text format for each file individually: the exact same
// <BeginBlock>...<EndBlock> tags FemmFileIO.cpp/HeatFileIO.cpp already
// read/write for a .fem/.feh's own BlockProps section, plus a
// <BeginFolder>/<FolderName>/.../<EndFolder> nesting for categorization
// that .fem/.feh files don't have. Confirmed directly against both
// files' real content.
struct MaterialLibraryNode {
  QString name;
  bool isFolder = true;
  FemmMaterialProp material; // meaningful only if !isFolder
  QVector<MaterialLibraryNode> children; // meaningful only if isFolder
};

namespace MaterialLibraryIO {

// `root` comes back as a synthetic top-level folder (name unset).
// matlib.dat's own folder tree is the primary structure (kept as-is);
// each of its leaf materials is matched against heatlib.dat's entries by
// NAME (case-insensitive, ignoring heatlib.dat's own, differently-
// organized folder structure) and merged in place when a match is
// found. Any heatlib.dat entry with no matching name anywhere in
// matlib.dat is appended under a new top-level "Heat Flow Materials
// (no magnetics match)" folder, so nothing from either source file is
// silently dropped. Errors from either file are reported (via
// `errorMessage`, newline-joined if both fail) but loading continues
// with whichever file(s) succeeded -- a missing/unreadable heatlib.dat
// still leaves a fully usable magnetics-only library, and vice versa.
bool load(const QString& matlibPath, const QString& heatlibPath, MaterialLibraryNode& root, QString& errorMessage);

} // namespace MaterialLibraryIO
