#include "MaterialLibraryIO.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QTextStream>

namespace {

QString unquote(const QString& s)
{
  QString t = s.trimmed();
  if (t.length() >= 2 && t.startsWith('"') && t.endsWith('"'))
    return t.mid(1, t.length() - 2);
  return t;
}

bool splitTagValue(const QString& line, QString& tag, QString& value)
{
  int eq = line.indexOf('=');
  if (eq < 0)
    return false;
  tag = line.left(eq).trimmed();
  if (tag.length() >= 2 && ((tag.front() == '[' && tag.back() == ']') || (tag.front() == '<' && tag.back() == '>')))
    tag = tag.mid(1, tag.length() - 2);
  value = line.mid(eq + 1).trimmed();
  return true;
}

QVector<QString> splitFields(const QString& line)
{
  static const QRegularExpression ws("\\s+");
  QVector<QString> out;
  for (const QString& tok : line.trimmed().split(ws, Qt::SkipEmptyParts))
    out.push_back(tok);
  return out;
}

// Parses one <BeginBlock>...<EndBlock> material's MAGNETIC fields --
// tags match FemmFileIO.cpp's BlockProps case exactly (both ultimately
// mirror femm/FemmeDoc.cpp's .fem writer for a CMaterialProp).
bool readMagBlock(QTextStream& in, FemmMaterialProp& m)
{
  QString line;
  while (!in.atEnd()) {
    line = in.readLine();
    if (line.trimmed() == "<EndBlock>")
      return true;
    QString t, v;
    if (!splitTagValue(line, t, v))
      continue;
    if (t == "BlockName")
      m.name = unquote(v);
    else if (t == "Mu_x")
      m.muX = v.toDouble();
    else if (t == "Mu_y")
      m.muY = v.toDouble();
    else if (t == "H_c")
      m.Hc = v.toDouble();
    else if (t == "H_cAngle")
      m.HcAngle = v.toDouble();
    else if (t == "J_re")
      m.JsrcRe = v.toDouble();
    else if (t == "J_im")
      m.JsrcIm = v.toDouble();
    else if (t == "Sigma")
      m.sigma = v.toDouble();
    else if (t == "d_lam")
      m.dLam = v.toDouble();
    else if (t == "Phi_h")
      m.phiH = v.toDouble();
    else if (t == "Phi_hx")
      m.phiHx = v.toDouble();
    else if (t == "Phi_hy")
      m.phiHy = v.toDouble();
    else if (t == "LamType")
      m.lamType = v.toInt();
    else if (t == "LamFill")
      m.lamFill = v.toDouble();
    else if (t == "NStrands")
      m.nStrands = v.toInt();
    else if (t == "WireD")
      m.wireD = v.toDouble();
    else if (t == "BHPoints") {
      int bhPoints = v.toInt();
      for (int k = 0; k < bhPoints && !in.atEnd(); k++) {
        QVector<QString> f = splitFields(in.readLine());
        if (f.size() >= 2)
          m.bhData.push_back({ f[0].toDouble(), f[1].toDouble() });
      }
    }
  }
  return false; // ran off the end of the file without <EndBlock>
}

// Parses the children of a matlib.dat folder (or the implicit top-level
// "folder") until a matching <EndFolder> (or end of file, for the top
// level).
void readMagChildren(QTextStream& in, MaterialLibraryNode& node)
{
  while (!in.atEnd()) {
    QString line = in.readLine().trimmed();
    if (line == "<EndFolder>")
      return;
    if (line == "<BeginFolder>") {
      MaterialLibraryNode child;
      child.isFolder = true;
      // <FolderName> is always the line right after <BeginFolder> in
      // matlib.dat -- read it directly rather than looping, matching
      // femm/fe_libdlg.cpp's own writer, which always emits them adjacent.
      QString nameLine = in.readLine();
      QString t, v;
      if (splitTagValue(nameLine, t, v) && t == "FolderName")
        child.name = unquote(v);
      readMagChildren(in, child);
      node.children.push_back(child);
    } else if (line == "<BeginBlock>") {
      MaterialLibraryNode child;
      child.isFolder = false;
      if (readMagBlock(in, child.material)) {
        // MaterialLibraryNode::name is what populateTree() displays --
        // for a folder it comes from <FolderName>, but a block's own name
        // only ever lands in child.material.name (set by readMagBlock()
        // from <BlockName>).
        child.name = child.material.name;
        node.children.push_back(child);
      }
    }
  }
}

// Flat name(lowercased) -> thermal fields, built from heatlib.dat --
// heatlib.dat's own folder structure ("Metallic Solids"/"Gases at 1
// atm"/...) doesn't correspond to matlib.dat's ("Iron & Steel"/"Coils"/
// ...), so rather than a doomed folder-path merge, materials are matched
// by NAME alone across the two files.
struct ThermalFields {
  double Kx = 0, Ky = 0, Kt = 0, qv = 0;
  QVector<QPair<double, double>> tkData;
};

bool readHeatBlock(QTextStream& in, QString& name, ThermalFields& t)
{
  QString line;
  while (!in.atEnd()) {
    line = in.readLine();
    if (line.trimmed() == "<EndBlock>")
      return true;
    QString tag, v;
    if (!splitTagValue(line, tag, v))
      continue;
    if (tag == "BlockName")
      name = unquote(v);
    else if (tag == "Kx")
      t.Kx = v.toDouble();
    else if (tag == "Ky")
      t.Ky = v.toDouble();
    else if (tag == "Kt")
      t.Kt = v.toDouble();
    else if (tag == "qv")
      t.qv = v.toDouble();
    else if (tag == "TKPoints") {
      int n = v.toInt();
      for (int k = 0; k < n && !in.atEnd(); k++) {
        QVector<QString> f = splitFields(in.readLine());
        if (f.size() >= 2)
          t.tkData.push_back({ f[0].toDouble(), f[1].toDouble() });
      }
    }
  }
  return false;
}

void readHeatChildren(QTextStream& in, QHash<QString, ThermalFields>& flat)
{
  while (!in.atEnd()) {
    QString line = in.readLine().trimmed();
    if (line == "<EndFolder>")
      return;
    if (line == "<BeginFolder>") {
      // Folder name itself is irrelevant here -- heatlib.dat's structure
      // is discarded, only leaf name -> fields matters (see ThermalFields'
      // comment). Still needs to be consumed (one line) to keep the
      // stream aligned.
      in.readLine();
      readHeatChildren(in, flat);
    } else if (line == "<BeginBlock>") {
      QString name;
      ThermalFields t;
      if (readHeatBlock(in, name, t) && !name.isEmpty())
        flat.insert(name.toLower(), t);
    }
  }
}

// Recursively applies a match from `flat` (removing it once consumed) to
// every leaf in `node`, by name.
void mergeThermalInto(MaterialLibraryNode& node, QHash<QString, ThermalFields>& flat)
{
  if (node.isFolder) {
    for (MaterialLibraryNode& child : node.children)
      mergeThermalInto(child, flat);
    return;
  }
  auto it = flat.find(node.material.name.toLower());
  if (it != flat.end()) {
    node.material.Kx = it->Kx;
    node.material.Ky = it->Ky;
    node.material.Kt = it->Kt;
    node.material.qv = it->qv;
    node.material.tkData = it->tkData;
    flat.erase(it);
  }
}

} // namespace

bool MaterialLibraryIO::load(const QString& matlibPath, const QString& heatlibPath, MaterialLibraryNode& root, QString& errorMessage)
{
  root = MaterialLibraryNode();
  root.isFolder = true;
  QStringList errors;

  QFile matFile(matlibPath);
  if (matFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&matFile);
    readMagChildren(in, root);
  } else {
    errors << QStringLiteral("Could not open \"%1\" for reading.").arg(matlibPath);
  }

  QHash<QString, ThermalFields> heatFlat;
  QFile heatFile(heatlibPath);
  if (heatFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&heatFile);
    readHeatChildren(in, heatFlat);
  } else {
    errors << QStringLiteral("Could not open \"%1\" for reading.").arg(heatlibPath);
  }

  mergeThermalInto(root, heatFlat);

  // Whatever's left in heatFlat had no name match anywhere in matlib.dat
  // -- keep them rather than silently dropping heatlib.dat-only materials
  // (e.g. "Water" or gas entries with no magnetics-library counterpart).
  if (!heatFlat.isEmpty()) {
    MaterialLibraryNode extra;
    extra.isFolder = true;
    extra.name = "Heat Flow Materials (no magnetics match)";
    for (auto it = heatFlat.constBegin(); it != heatFlat.constEnd(); ++it) {
      MaterialLibraryNode leaf;
      leaf.isFolder = false;
      leaf.material.name = it.key();
      // it.key() is lowercased (the map's own key normalization) --
      // display/leaf material name should keep its original casing,
      // which the map itself doesn't retain, so leaf.material.name falls
      // back to the lowercased key here as a small, disclosed exception
      // rather than tracking a separate original-casing map for a
      // secondary, fallback-only display path.
      leaf.material.Kx = it->Kx;
      leaf.material.Ky = it->Ky;
      leaf.material.Kt = it->Kt;
      leaf.material.qv = it->qv;
      leaf.material.tkData = it->tkData;
      extra.children.push_back(leaf);
    }
    root.children.push_back(extra);
  }

  errorMessage = errors.join("\n");
  return errors.size() < 2; // fully failed only if BOTH files were unreadable
}
