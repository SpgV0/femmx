#pragma once

#include "FemmProblem.h"

#include <QString>
#include <QVector>

// Reads bin/heatlib.dat -- heat flow's counterpart to matlib.dat (femm/
// hd_libdlg.cpp's shared material library). Same <BeginFolder>/
// <FolderName>/.../<EndFolder> nesting and <BeginBlock>...<EndBlock> tags
// as MaterialLibraryIO's matlib.dat reader, just with heatlib.dat's own
// tag set (<Kx>/<Ky>/<Kt>/<qv>/<TKPoints>, matching HeatFileIO's BlockProps
// case) -- confirmed directly against bin/heatlib.dat's real content.
// Deliberately a separate file/struct from MaterialLibraryIO rather than a
// templated/merged reader -- matlib.dat and heatlib.dat are two distinct
// canonical files the classic GUI itself keeps separate (see FemmProblem.h's
// Round 6 comment on why femmqt follows that same precedent), and femmqt
// must stay a faithful, independently-loadable reader/writer of each.
struct ThermalMaterialLibraryNode {
  QString name;
  bool isFolder = true;
  FemmThermalMaterialProp material; // meaningful only if !isFolder
  QVector<ThermalMaterialLibraryNode> children; // meaningful only if isFolder
};

namespace ThermalMaterialLibraryIO {

bool load(const QString& path, ThermalMaterialLibraryNode& root, QString& errorMessage);

} // namespace ThermalMaterialLibraryIO
