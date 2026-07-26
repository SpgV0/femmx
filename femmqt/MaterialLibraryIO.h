#pragma once

#include "FemmProblem.h"

#include <QString>
#include <QVector>

// Reads bin/matlib.dat -- femm/fe_libdlg.cpp's shared material library,
// the "Materials Library" the classic GUI's Material Properties dialog
// offers alongside a problem's own per-file material list. Text format:
// the exact same <BeginBlock>...<EndBlock> tags FemmFileIO.cpp already
// reads/writes for a .fem's own BlockProps section (see that file's
// BlockProps case), plus a <BeginFolder>/<FolderName>/.../<EndFolder>
// nesting for categorization that .fem files don't have. Confirmed
// directly against bin/matlib.dat's real content.
struct MaterialLibraryNode {
  QString name;
  bool isFolder = true;
  FemmMaterialProp material; // meaningful only if !isFolder
  QVector<MaterialLibraryNode> children; // meaningful only if isFolder
};

namespace MaterialLibraryIO {

// `root` comes back as a synthetic top-level folder (name unset,
// children = whatever was at the top of the file, folders and blocks
// interleaved exactly as matlib.dat has them).
bool load(const QString& path, MaterialLibraryNode& root, QString& errorMessage);

} // namespace MaterialLibraryIO
