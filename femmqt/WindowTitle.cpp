#include "WindowTitle.h"

#include "ProblemKind.h"

#include <QFileInfo>

namespace {

const char* const kApp = "FEMMX (Qt)";

} // namespace

QString WindowTitle::forEditor(FemmProblemKind kind, const QString& path,
    bool dirty, const QString& demoTitle)
{
  QString name;
  if (!demoTitle.isEmpty()) {
    name = QStringLiteral("%1 (demo copy)").arg(demoTitle);
  } else if (path.isEmpty()) {
    name = QStringLiteral("Untitled");
  } else {
    name = path;
  }

  return QStringLiteral("%1 - %2 - %3%4")
      .arg(QLatin1String(kApp), ProblemKind::displayName(kind), name,
          dirty ? QStringLiteral("*") : QString());
}

QString WindowTitle::forSolution(FemmProblemKind kind, const QString& path)
{
  // The file name, not the whole path: a solution is opened from
  // somewhere the user just chose, and the directory is rarely the
  // interesting half.
  const QString name = path.isEmpty() ? QStringLiteral("Untitled")
                                      : QFileInfo(path).fileName();
  return QStringLiteral("%1 - %2 Solution - %3")
      .arg(QLatin1String(kApp), ProblemKind::displayName(kind), name);
}
