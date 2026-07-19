#include "MeshOverlay.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

namespace {
bool readFirstInt(QTextStream& in, int& value)
{
  QString line;
  do {
    if (in.atEnd())
      return false;
    line = in.readLine();
  } while (line.trimmed().isEmpty() || line.trimmed().startsWith('#'));
  value = line.trimmed().split(QRegularExpression("\\s+")).value(0).toInt();
  return true;
}
}

bool MeshOverlayIO::load(const QString& rootPath, MeshOverlay& mesh, QString& errorMessage)
{
  mesh.nodes.clear();
  mesh.elements.clear();

  QFile nodeFile(rootPath + ".node");
  if (!nodeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    errorMessage = "No mesh to display -- run Create Mesh first.";
    return false;
  }
  QTextStream nodeIn(&nodeFile);
  int nodeCount = 0;
  if (!readFirstInt(nodeIn, nodeCount)) {
    errorMessage = QStringLiteral("%1.node is empty or malformed.").arg(rootPath);
    return false;
  }
  mesh.nodes.resize(nodeCount);
  static const QRegularExpression ws("\\s+");
  for (int i = 0; i < nodeCount && !nodeIn.atEnd(); i++) {
    QStringList f = nodeIn.readLine().trimmed().split(ws, Qt::SkipEmptyParts);
    if (f.size() >= 3)
      mesh.nodes[i] = { f[1].toDouble(), f[2].toDouble() };
  }

  QFile eleFile(rootPath + ".ele");
  if (!eleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    errorMessage = "No mesh to display -- run Create Mesh first.";
    return false;
  }
  QTextStream eleIn(&eleFile);
  int eleCount = 0;
  if (!readFirstInt(eleIn, eleCount)) {
    errorMessage = QStringLiteral("%1.ele is empty or malformed.").arg(rootPath);
    return false;
  }
  mesh.elements.resize(eleCount);
  for (int i = 0; i < eleCount && !eleIn.atEnd(); i++) {
    QStringList f = eleIn.readLine().trimmed().split(ws, Qt::SkipEmptyParts);
    if (f.size() >= 4)
      mesh.elements[i] = { f[1].toInt(), f[2].toInt(), f[3].toInt() };
  }

  return true;
}
