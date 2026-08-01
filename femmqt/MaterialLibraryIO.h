#pragma once

#include "FemmProblem.h"

#include <QString>
#include <QVector>

// Reads bin/matlib.dat (femm/fe_libdlg.cpp's shared magnetics material
// library) into a tree of FemmMaterialProp entries. Text format: the exact
// same <BeginBlock>...<EndBlock> tags FemmFileIO.cpp already reads/writes
// for a .fem's own BlockProps section, plus a <BeginFolder>/<FolderName>/
// .../<EndFolder> nesting for categorization that .fem files don't have.
// Confirmed directly against the real file's content.
struct MaterialLibraryNode {
  QString name;
  bool isFolder = true;
  FemmMaterialProp material; // meaningful only if !isFolder
  QVector<MaterialLibraryNode> children; // meaningful only if isFolder
};

namespace MaterialLibraryIO {

// `root` comes back as a synthetic top-level folder (name unset).
bool load(const QString& matlibPath, MaterialLibraryNode& root, QString& errorMessage);

} // namespace MaterialLibraryIO
