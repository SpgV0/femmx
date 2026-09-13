#include "FileRouting.h"

#include <QFileInfo>

bool FileRouting::isSolutionFile(const QString& path)
{
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
  // (issue #83): each solver writes its own solution format, so all four
  // route to the viewer rather than to the editor. .ansx stays here too
  // -- it is the magnetics cache, and opening it means opening the
  // solution it caches.
  //
  // Getting this wrong sends a .anh to MainWindow, which opens it as a
  // MODEL: the geometry and properties parse fine (a solution file is its
  // input file plus a section), so it silently appears as an unsolved
  // heat-flow problem with the solution quietly dropped.
  const QString suffix = QFileInfo(path).suffix();
  for (const char* ext : { "ans", "res", "anh", "anc", "ansx" }) {
    if (suffix.compare(QLatin1String(ext), Qt::CaseInsensitive) == 0)
      return true;
  }
  return false;
}
