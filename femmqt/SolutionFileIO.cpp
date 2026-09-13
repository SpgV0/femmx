#include "SolutionFileIO.h"

#include "FemmProblem.h"
#include "FemmTextFormat.h"
#include "ProblemFileIO.h"
#include "ProblemKind.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

using FemmTextFormat::splitFields;

namespace {

struct SolutionExt {
  const char* extension;
  FemmProblemKind kind;
};

// The mapping is ProblemKind's solutionExtension() read backwards; kept
// as its own table so a solution extension can never be mistaken for a
// model one (see kindForSolutionPath).
constexpr SolutionExt kSolutionExts[] = {
  { "ans", FemmProblemKind::Magnetics },
  { "res", FemmProblemKind::Electrostatics },
  { "anh", FemmProblemKind::HeatFlow },
  { "anc", FemmProblemKind::CurrentFlow },
};

} // namespace

bool SolutionFileIO::kindForSolutionPath(const QString& path, FemmProblemKind& kindOut)
{
  const QString suffix = QFileInfo(path).suffix().toLower();
  for (const SolutionExt& e : kSolutionExts) {
    if (suffix == QLatin1String(e.extension)) {
      kindOut = e.kind;
      return true;
    }
  }
  return false;
}

bool SolutionFileIO::read(const QString& path, FemmProblem& problem, SolvedMesh& mesh,
    QString& errorMessage)
{
  mesh = SolvedMesh();

  FemmProblemKind kind = FemmProblemKind::Magnetics;
  if (!kindForSolutionPath(path, kind)) {
    errorMessage = QStringLiteral(
        "\"%1\" is not a FEMM solution file. Expected .ans (magnetics), .res "
        "(electrostatics), .anh (heat flow) or .anc (current flow).")
                       .arg(QFileInfo(path).fileName());
    return false;
  }

  // The model half. A solution file is its input file with a section
  // appended, so this is the same reader, and it stops caring at the
  // [Solution] marker because that tag is not one it knows.
  if (!ProblemFileIO::readAs(path, kind, problem, errorMessage))
    return false;

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    errorMessage = QStringLiteral("Could not open \"%1\" for reading.").arg(path);
    return false;
  }
  QTextStream in(&file);

  // Find the marker. Everything before it has already been read above.
  bool found = false;
  QString line;
  while (!in.atEnd()) {
    line = in.readLine();
    if (line.trimmed().startsWith(QLatin1String("[Solution]"), Qt::CaseInsensitive)) {
      found = true;
      break;
    }
  }
  if (!found) {
    errorMessage = QStringLiteral(
        "\"%1\" has no [Solution] section -- it is an unsolved model, not a "
        "solution. Run the solver first.")
                       .arg(QFileInfo(path).fileName());
    return false;
  }

  // Magnetics is the only one whose node line changes width, and it
  // changes on a value from the header this reader has already parsed.
  // Getting this wrong does not fail: it eats the next line's x as an
  // imaginary part and shifts every node after it.
  const bool magneticsAc =
      (kind == FemmProblemKind::Magnetics) && (problem.frequency != 0);
  const bool hasConductorColumn = (kind != FemmProblemKind::Magnetics);

  auto nextLine = [&in](QString& out) -> bool {
    if (in.atEnd())
      return false;
    out = in.readLine();
    return true;
  };

  // Node count, then that many nodes.
  if (!nextLine(line)) {
    errorMessage = QStringLiteral("\"%1\" ends immediately after [Solution].")
                       .arg(QFileInfo(path).fileName());
    return false;
  }
  const int nodeCount = line.trimmed().toInt();
  mesh.nodes.reserve(nodeCount);
  for (int i = 0; i < nodeCount && nextLine(line); i++) {
    const QVector<QString> f = splitFields(line);
    if (f.size() < 3)
      continue;
    SolutionNode n;
    n.x = f[0].toDouble();
    n.y = f[1].toDouble();
    n.potentialRe = f[2].toDouble();

    int next = 3;
    if (kind == FemmProblemKind::CurrentFlow || magneticsAc) {
      if (f.size() > next)
        n.potentialIm = f[next++].toDouble();
    }
    if (hasConductorColumn && f.size() > next)
      n.conductor = f[next].toInt();

    mesh.nodes.push_back(n);
  }

  // Element count, then that many elements. Identical in all four.
  if (!nextLine(line)) {
    errorMessage = QStringLiteral("\"%1\" ends after its mesh nodes, with no "
                                  "element section.")
                       .arg(QFileInfo(path).fileName());
    return false;
  }
  const int elementCount = line.trimmed().toInt();
  mesh.elements.reserve(elementCount);
  for (int i = 0; i < elementCount && nextLine(line); i++) {
    const QVector<QString> f = splitFields(line);
    if (f.size() < 4)
      continue;
    SolutionElement e;
    e.p0 = f[0].toInt();
    e.p1 = f[1].toInt();
    e.p2 = f[2].toInt();
    e.label = f[3].toInt();
    mesh.elements.push_back(e);
  }

  if (mesh.nodes.size() != nodeCount || mesh.elements.size() != elementCount) {
    errorMessage = QStringLiteral(
        "\"%1\" declares %2 mesh nodes and %3 elements but only %4 and %5 could "
        "be read -- the file is truncated or malformed.")
                       .arg(QFileInfo(path).fileName())
                       .arg(nodeCount).arg(elementCount)
                       .arg(mesh.nodes.size()).arg(mesh.elements.size());
    return false;
  }
  return true;
}
