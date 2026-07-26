#include "ThermalMaterialLibraryIO.h"

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
// HeatFileIO.cpp's BlockProps case exactly (both ultimately mirror
// femm/HDRAWDOC.CPP's .feh writer for a CMaterialProp).
bool readBlock(QTextStream& in, FemmThermalMaterialProp& m)
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
    else if (t == "Kx")
      m.Kx = v.toDouble();
    else if (t == "Ky")
      m.Ky = v.toDouble();
    else if (t == "Kt")
      m.Kt = v.toDouble();
    else if (t == "qv")
      m.qv = v.toDouble();
    else if (t == "TKPoints") {
      int n = v.toInt();
      for (int k = 0; k < n && !in.atEnd(); k++) {
        QVector<QString> f = splitFields(in.readLine());
        if (f.size() >= 2)
          m.tkData.push_back({ f[0].toDouble(), f[1].toDouble() });
      }
    }
  }
  return false; // ran off the end of the file without <EndBlock>
}

// Parses the children of a folder (or the implicit top-level "folder")
// until a matching <EndFolder> (or end of file, for the top level).
void readChildren(QTextStream& in, ThermalMaterialLibraryNode& node)
{
  while (!in.atEnd()) {
    QString line = in.readLine().trimmed();
    if (line == "<EndFolder>")
      return;
    if (line == "<BeginFolder>") {
      ThermalMaterialLibraryNode child;
      child.isFolder = true;
      QString nameLine = in.readLine();
      QString t, v;
      if (splitTagValue(nameLine, t, v) && t == "FolderName")
        child.name = unquote(v);
      readChildren(in, child);
      node.children.push_back(child);
    } else if (line == "<BeginBlock>") {
      ThermalMaterialLibraryNode child;
      child.isFolder = false;
      if (readBlock(in, child.material)) {
        child.name = child.material.name;
        node.children.push_back(child);
      }
    }
  }
}

} // namespace

bool ThermalMaterialLibraryIO::load(const QString& path, ThermalMaterialLibraryNode& root, QString& errorMessage)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    errorMessage = QStringLiteral("Could not open \"%1\" for reading.").arg(path);
    return false;
  }
  root = ThermalMaterialLibraryNode();
  root.isFolder = true;
  QTextStream in(&file);
  readChildren(in, root);
  return true;
}
