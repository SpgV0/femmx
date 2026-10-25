#include "FileDialogDirs.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <memory>

namespace {

QString key(const QString& kind)
{
  return QStringLiteral("fileDialogs/") + kind;
}

QString documents()
{
  const QString d = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
  return d.isEmpty() ? QDir::homePath() : d;
}

} // namespace

QString FileDialogDirs::startDir(const QString& kind, const QString& currentFile, QSettings* settings)
{
  if (!currentFile.isEmpty()) {
    const QString dir = QFileInfo(currentFile).absolutePath();
    if (QFileInfo(dir).isDir())
      return dir;
  }
  std::unique_ptr<QSettings> own;
  if (!settings) {
    own = std::make_unique<QSettings>();
    settings = own.get();
  }
  const QString last = settings->value(key(kind)).toString();
  if (!last.isEmpty() && QFileInfo(last).isDir())
    return last;
  return documents();
}

QString FileDialogDirs::startPath(const QString& kind, const QString& fileName, const QString& currentFile,
    QSettings* settings)
{
  return QDir(startDir(kind, currentFile, settings)).filePath(fileName);
}

void FileDialogDirs::remember(const QString& kind, const QString& chosenPath, QSettings* settings)
{
  if (chosenPath.isEmpty())
    return;
  std::unique_ptr<QSettings> own;
  if (!settings) {
    own = std::make_unique<QSettings>();
    settings = own.get();
  }
  settings->setValue(key(kind), QFileInfo(chosenPath).absolutePath());
}
