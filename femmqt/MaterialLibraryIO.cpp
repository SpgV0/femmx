#include "MaterialLibraryIO.h"

#include "FemmTextFormat.h"
#include "ProblemKind.h"
#include "PropertyCodec.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

using FemmTextFormat::splitTagValue;
using FemmTextFormat::unquote;

namespace {

// Pulls the one material PropertyCodec just parsed out of a scratch
// problem and onto the node.
void takeMaterial(const FemmProblem& scratch, MaterialLibraryNode& node)
{
  switch (scratch.kind) {
  case FemmProblemKind::Magnetics:
    if (!scratch.materialProps.isEmpty())
      node.material = scratch.materialProps.last();
    return;
  case FemmProblemKind::Electrostatics:
    if (!scratch.esMaterialProps.isEmpty())
      node.esMaterial = scratch.esMaterialProps.last();
    return;
  case FemmProblemKind::HeatFlow:
    if (!scratch.htMaterialProps.isEmpty())
      node.htMaterial = scratch.htMaterialProps.last();
    return;
  case FemmProblemKind::CurrentFlow:
    if (!scratch.cfMaterialProps.isEmpty())
      node.cfMaterial = scratch.cfMaterialProps.last();
    return;
  }
}

QString nameOf(const MaterialLibraryNode& node, FemmProblemKind kind)
{
  switch (kind) {
  case FemmProblemKind::Magnetics: return node.material.name;
  case FemmProblemKind::Electrostatics: return node.esMaterial.name;
  case FemmProblemKind::HeatFlow: return node.htMaterial.name;
  case FemmProblemKind::CurrentFlow: return node.cfMaterial.name;
  }
  return QString();
}

// Reads the children of the folder that is currently open, stopping at
// its <EndFolder> or at end of input.
//
// Recursion mirrors the file's own nesting; heatlib.dat is two deep
// ("Metallic Solids" -> "Aluminum" -> the alloys).
void readFolderBody(QTextStream& in, FemmProblemKind kind, MaterialLibraryNode& parent)
{
  const PropertyCodec::LineReader next = [&in](QString& out) -> bool {
    if (in.atEnd())
      return false;
    out = in.readLine();
    return true;
  };

  QString line;
  while (next(line)) {
    const QString trimmed = line.trimmed();
    if (trimmed == "<EndFolder>")
      return;

    if (trimmed == "<BeginFolder>") {
      MaterialLibraryNode folder;
      folder.isFolder = true;
      readFolderBody(in, kind, folder);
      parent.children.push_back(folder);
      continue;
    }

    if (trimmed == "<BeginBlock>") {
      // A library leaf IS a [BlockProps] record, so the same codec that
      // reads a model's materials reads this one. readSection consumes
      // up to and including <EndBlock>.
      FemmProblem scratch;
      scratch.kind = kind;
      PropertyCodec::readSection(scratch, QStringLiteral("BlockProps"), 1, next);

      MaterialLibraryNode leaf;
      leaf.isFolder = false;
      takeMaterial(scratch, leaf);
      leaf.name = nameOf(leaf, kind);
      parent.children.push_back(leaf);
      continue;
    }

    // A folder's name arrives as a tag AFTER its <BeginFolder>, so it
    // lands here, inside the folder it names.
    QString tag, value;
    if (splitTagValue(line, tag, value) && tag == "FolderName")
      parent.name = unquote(value);
  }
}

} // namespace

QString MaterialLibraryIO::defaultFileName(FemmProblemKind kind)
{
  switch (kind) {
  case FemmProblemKind::Magnetics: return QStringLiteral("matlib.dat");
  case FemmProblemKind::Electrostatics: return QStringLiteral("statlib.dat");
  case FemmProblemKind::HeatFlow: return QStringLiteral("heatlib.dat");
  case FemmProblemKind::CurrentFlow: return QStringLiteral("condlib.dat");
  }
  return QStringLiteral("matlib.dat");
}

bool MaterialLibraryIO::load(const QString& path, FemmProblemKind kind,
    MaterialLibraryNode& root, QString& errorMessage)
{
  root = MaterialLibraryNode();
  root.isFolder = true;
  root.name = ProblemKind::displayName(kind);

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    errorMessage = QStringLiteral("Could not open the %1 material library \"%2\".")
                       .arg(ProblemKind::displayName(kind), QFileInfo(path).fileName());
    return false;
  }

  QTextStream in(&file);
  // The top level has no <BeginFolder> of its own, so it is read as the
  // body of a folder that never ends.
  readFolderBody(in, kind, root);
  root.name = ProblemKind::displayName(kind);
  return true;
}

int MaterialLibraryIO::appendTo(FemmProblem& p, const MaterialLibraryNode& node)
{
  if (node.isFolder)
    return -1;

  // Disambiguate against what is already in the problem. Two materials
  // sharing a name is not cosmetic -- the writer identifies a material
  // BY NAME, so the second becomes unreachable (the defect #34 fixed in
  // these very libraries).
  QSet<QString> taken;
  const int existing = ProblemKind::count(p, ProblemKind::Category::Material);
  for (int i = 0; i < existing; i++)
    taken.insert(ProblemKind::name(p, ProblemKind::Category::Material, i));

  const QString base = nameOf(node, p.kind);
  QString name = base;
  for (int n = 2; taken.contains(name); n++)
    name = QStringLiteral("%1 (%2)").arg(base).arg(n);

  switch (p.kind) {
  case FemmProblemKind::Magnetics: p.materialProps.push_back(node.material); break;
  case FemmProblemKind::Electrostatics: p.esMaterialProps.push_back(node.esMaterial); break;
  case FemmProblemKind::HeatFlow: p.htMaterialProps.push_back(node.htMaterial); break;
  case FemmProblemKind::CurrentFlow: p.cfMaterialProps.push_back(node.cfMaterial); break;
  }

  const int index = ProblemKind::count(p, ProblemKind::Category::Material) - 1;
  ProblemKind::setName(p, ProblemKind::Category::Material, index, name);
  return index;
}
