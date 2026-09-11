#include "FileRouting.h"

#include <QFileInfo>

bool FileRouting::isSolutionFile(const QString& path)
{
  const QString suffix = QFileInfo(path).suffix();
  return suffix.compare(QStringLiteral("ans"), Qt::CaseInsensitive) == 0
      || suffix.compare(QStringLiteral("ansx"), Qt::CaseInsensitive) == 0;
}
