#include "DemoLibrary.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {

// Deep enough for both layouts and no deeper: a dev build sits at
// <repo>/build_qt/femmqt/Release, an install at $INSTDIR\bin. Walking
// further would eventually find some unrelated "demos" directory on the
// way to the drive root.
const int kMaxLevelsUp = 5;

QString g_cachedDir;
bool g_searched = false;

} // namespace

QString DemoLibrary::Demo::absolutePath() const
{
  const QString root = directory();
  if (root.isEmpty() || file.isEmpty())
    return QString();
  return QDir(root).absoluteFilePath(file);
}

QString DemoLibrary::directory()
{
  if (g_searched)
    return g_cachedDir;
  g_searched = true;

  QDir dir(QCoreApplication::applicationDirPath());
  for (int level = 0; level <= kMaxLevelsUp; level++) {
    // The manifest, not just the directory name -- a directory called
    // "demos" holding something else is not this.
    const QString candidate = dir.absoluteFilePath(QStringLiteral("demos"));
    if (QFileInfo::exists(candidate + QStringLiteral("/demos.json"))) {
      g_cachedDir = QDir(candidate).absolutePath();
      return g_cachedDir;
    }
    if (!dir.cdUp())
      break;
  }
  return g_cachedDir; // empty: no corpus in this tree, which is allowed
}

QVector<DemoLibrary::Demo> DemoLibrary::load(QString& error)
{
  QVector<Demo> demos;

  const QString root = directory();
  if (root.isEmpty()) {
    error = QStringLiteral("no demos directory was found next to femmqt.exe");
    return demos;
  }

  QFile f(root + QStringLiteral("/demos.json"));
  if (!f.open(QIODevice::ReadOnly)) {
    error = QStringLiteral("could not read %1").arg(f.fileName());
    return demos;
  }

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
  if (doc.isNull() || !doc.isObject()) {
    error = QStringLiteral("%1 is not valid JSON: %2")
                .arg(f.fileName(), parseError.errorString());
    return demos;
  }

  const QJsonObject obj = doc.object();
  const QJsonArray array = obj.value(QStringLiteral("demos")).toArray();
  if (array.isEmpty()) {
    error = QStringLiteral("%1 lists no demos").arg(f.fileName());
    return demos;
  }

  for (const QJsonValue& v : array) {
    const QJsonObject o = v.toObject();
    Demo d;
    d.file = o.value(QStringLiteral("file")).toString();
    d.title = o.value(QStringLiteral("title")).toString();
    d.problemType = o.value(QStringLiteral("problemType")).toString();
    d.description = o.value(QStringLiteral("description")).toString();
    d.analyticReference = o.value(QStringLiteral("analyticReference")).toString();
    if (d.file.isEmpty())
      continue; // an entry naming no file is not openable
    // A manifest entry for a file that is not installed would show a
    // row that cannot be opened, which is worse than a shorter list.
    if (!QFileInfo::exists(QDir(root).absoluteFilePath(d.file)))
      continue;
    if (d.title.isEmpty())
      d.title = QFileInfo(d.file).completeBaseName();
    demos << d;
  }

  if (demos.isEmpty())
    error = QStringLiteral("%1 lists demos, but none of the files are present")
                .arg(f.fileName());
  return demos;
}

QString DemoLibrary::workingCopyRoot()
{
  const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  return QDir(base).absoluteFilePath(QStringLiteral("femmqt-demos"));
}

QString DemoLibrary::makeWorkingCopy(const QString& demoPath, QString& error)
{
  const QFileInfo source(demoPath);
  if (!source.exists()) {
    error = QStringLiteral("\"%1\" is not there").arg(demoPath);
    return QString();
  }

  // A directory per copy. Two demos with the same base name -- or the
  // same demo opened twice -- would otherwise share a folder, and the
  // solve pipeline writes by base name, so one solve's mesh would land
  // on the other's.
  QDir root(workingCopyRoot());
  if (!root.mkpath(QStringLiteral("."))) {
    error = QStringLiteral("could not create %1").arg(root.absolutePath());
    return QString();
  }

  const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-hhmmsszzz");
  QString sub = QStringLiteral("%1-%2").arg(source.completeBaseName(), stamp);
  int n = 1;
  while (root.exists(sub)) {
    sub = QStringLiteral("%1-%2-%3").arg(source.completeBaseName(), stamp).arg(++n);
  }
  if (!root.mkpath(sub)) {
    error = QStringLiteral("could not create %1").arg(root.absoluteFilePath(sub));
    return QString();
  }

  const QString target = QDir(root.absoluteFilePath(sub)).absoluteFilePath(source.fileName());
  if (!QFile::copy(demoPath, target)) {
    error = QStringLiteral("could not copy \"%1\" to \"%2\"").arg(demoPath, target);
    return QString();
  }
  // The copy inherits the original's read-only attribute on Windows,
  // and a read-only working copy defeats the entire point.
  QFile(target).setPermissions(QFile::ReadOwner | QFile::WriteOwner
      | QFile::ReadUser | QFile::WriteUser);

  // The sketch sidecar is part of the model, not a regenerable cache
  // (see SketchFileIO.h), so it travels. Everything else beside a demo
  // -- .femx, solutions, mesh files -- is derived and would only go
  // stale in the copy.
  const QString sketch = source.absolutePath() + "/" + source.completeBaseName() + ".fes";
  if (QFileInfo::exists(sketch)) {
    const QString sketchTarget = QDir(root.absoluteFilePath(sub))
                                     .absoluteFilePath(source.completeBaseName() + ".fes");
    QFile::copy(sketch, sketchTarget);
    QFile(sketchTarget).setPermissions(QFile::ReadOwner | QFile::WriteOwner
        | QFile::ReadUser | QFile::WriteUser);
  }

  return target;
}
