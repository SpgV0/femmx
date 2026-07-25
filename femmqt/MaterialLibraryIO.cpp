#include "MaterialLibraryIO.h"

#include <QFile>
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

// Parses one <BeginBlock>...<EndBlock> material -- field set/tags match
// FemmFileIO.cpp's BlockProps case exactly (both ultimately mirror
// femm/FemmeDoc.cpp's .fem writer for a CMaterialProp).
bool readBlock(QTextStream& in, FemmMaterialProp& m)
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

// Parses the children of a folder (or the implicit top-level "folder")
// until a matching <EndFolder> (or end of file, for the top level).
void readChildren(QTextStream& in, MaterialLibraryNode& node)
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
      readChildren(in, child);
      node.children.push_back(child);
    } else if (line == "<BeginBlock>") {
      MaterialLibraryNode child;
      child.isFolder = false;
      if (readBlock(in, child.material)) {
        // MaterialLibraryNode::name is what populateTree() displays --
        // for a folder it comes from <FolderName>, but a block's own name
        // only ever lands in child.material.name (set by readBlock() from
        // <BlockName>). Without this, every leaf material in the tree
        // renders as a real, selectable, functional item with no visible
        // text at all -- confirmed directly: clicking the blank row and
        // hitting Add to Problem correctly added the right material by
        // name, proving the data was always fine and only the tree
        // display's copy of the name was missing.
        child.name = child.material.name;
        node.children.push_back(child);
      }
    }
  }
}

} // namespace

bool MaterialLibraryIO::load(const QString& path, MaterialLibraryNode& root, QString& errorMessage)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    errorMessage = QStringLiteral("Could not open \"%1\" for reading.").arg(path);
    return false;
  }
  root = MaterialLibraryNode();
  root.isFolder = true;
  QTextStream in(&file);
  readChildren(in, root);
  return true;
}
