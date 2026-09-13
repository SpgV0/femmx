#pragma once

// MaterialLibraryIO.h
//
// The shipped material libraries: matlib.dat (magnetics), statlib.dat
// (electrostatics), heatlib.dat (heat flow) and condlib.dat (current
// flow).
//
// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #81): all four, not just magnetics.
//
// All four files are the same format -- a tree of <BeginFolder> nodes
// whose leaves are <BeginBlock> records -- and a leaf is byte-for-byte a
// [BlockProps] record of that physics. So the field parsing is not
// reimplemented here: it goes through PropertyCodec, the same code that
// reads a model file's materials. Before this, the loader carried its
// own copy of the magnetics field list and its own copy of the tag/value
// helpers, which is how a library and a model file drift into disagreeing
// about the same record.

#include "FemmProblem.h"

#include <QString>
#include <QVector>

struct MaterialLibraryNode {
  QString name;
  bool isFolder = true;

  // Whichever one matches the library's kind carries the material; the
  // others stay default. A leaf is small, and a library has hundreds of
  // them, so this is four small structs rather than a whole FemmProblem
  // per entry.
  FemmMaterialProp material;     // Magnetics
  FemmEsMaterialProp esMaterial; // Electrostatics
  FemmHtMaterialProp htMaterial; // Heat Flow
  FemmCfMaterialProp cfMaterial; // Current Flow

  QVector<MaterialLibraryNode> children; // meaningful only if isFolder
};

namespace MaterialLibraryIO {

// "matlib.dat" / "statlib.dat" / "heatlib.dat" / "condlib.dat" -- the
// file name only; the caller supplies the directory.
QString defaultFileName(FemmProblemKind kind);

// Loads a library of `kind`. A missing file is a failure with a
// user-presentable reason rather than an empty tree, so "the library is
// empty" and "the library is not installed" do not look the same.
bool load(const QString& path, FemmProblemKind kind, MaterialLibraryNode& root,
    QString& errorMessage);

// Appends `node`'s material to `p`'s own material list, under a name
// that does not collide with one already there, and returns its index.
//
// The kinds must match: importing an electrostatics material into a heat
// flow problem has no meaning, and silently importing the default-
// constructed struct for the wrong kind would add a material of all
// zeroes -- a region that conducts nothing, meshed and solved.
int appendTo(FemmProblem& p, const MaterialLibraryNode& node);

} // namespace MaterialLibraryIO
