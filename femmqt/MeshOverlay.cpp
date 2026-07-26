#include "MeshOverlay.h"

#include <QFile>

#include <cstdlib>

namespace {
// Skips blank/comment ('#') lines and returns the first whitespace-
// separated integer on the next real line -- only runs once per file
// (the header/count line), so QByteArray::trimmed()/startsWith() here
// costs nothing; the per-row parsing below is the part that scales with
// mesh size and needs to stay allocation-free.
bool readFirstInt(QFile& file, long& value)
{
  QByteArray line;
  do {
    if (file.atEnd())
      return false;
    line = file.readLine();
  } while (line.trimmed().isEmpty() || line.trimmed().startsWith('#'));
  value = std::strtol(line.constData(), nullptr, 10);
  return true;
}
}

bool MeshOverlayIO::load(const QString& rootPath, MeshOverlay& mesh, QString& errorMessage)
{
  mesh.nodes.clear();
  mesh.elements.clear();

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
  // was QTextStream + QRegularExpression(\s+)::split() per line -- the
  // exact per-line regex/QString-allocation pattern CHANGELOG.md's
  // v1.1.1 fix already had to remove from the .ans reader for the same
  // reason (femm/FemmviewDoc.cpp / AnsFileIO.cpp both use manual
  // strtol/strtod instead): triangle.exe's .node/.ele output for a large,
  // dense mesh -- e.g. the fine-detail zoom case this session's "litz
  // wire" density-plot fix targets -- can run to millions of lines, and
  // Show Mesh reads it on every "Create Mesh"/toggle, not just once at
  // load like .ansx's cache does.
  QFile nodeFile(rootPath + ".node");
  if (!nodeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    errorMessage = "No mesh to display -- run Create Mesh first.";
    return false;
  }
  long nodeCount = 0;
  if (!readFirstInt(nodeFile, nodeCount)) {
    errorMessage = QStringLiteral("%1.node is empty or malformed.").arg(rootPath);
    return false;
  }
  mesh.nodes.resize((int)nodeCount);
  for (long i = 0; i < nodeCount && !nodeFile.atEnd(); i++) {
    QByteArray line = nodeFile.readLine();
    const char* p = line.constData();
    char* next = nullptr;
    std::strtol(p, &next, 10); // node index, unused -- rows are already in index order
    p = next;
    double x = std::strtod(p, &next);
    p = next;
    double y = std::strtod(p, &next);
    mesh.nodes[(int)i] = { x, y };
  }

  QFile eleFile(rootPath + ".ele");
  if (!eleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    errorMessage = "No mesh to display -- run Create Mesh first.";
    return false;
  }
  long eleCount = 0;
  if (!readFirstInt(eleFile, eleCount)) {
    errorMessage = QStringLiteral("%1.ele is empty or malformed.").arg(rootPath);
    return false;
  }
  mesh.elements.resize((int)eleCount);
  for (long i = 0; i < eleCount && !eleFile.atEnd(); i++) {
    QByteArray line = eleFile.readLine();
    const char* p = line.constData();
    char* next = nullptr;
    std::strtol(p, &next, 10); // element index, unused
    p = next;
    int p0 = (int)std::strtol(p, &next, 10);
    p = next;
    int p1 = (int)std::strtol(p, &next, 10);
    p = next;
    int p2 = (int)std::strtol(p, &next, 10);
    mesh.elements[(int)i] = { p0, p1, p2 };
  }

  return true;
}
