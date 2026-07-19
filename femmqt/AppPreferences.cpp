#include "AppPreferences.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

namespace {

QString cfgPath()
{
  return QCoreApplication::applicationDirPath() + "/femm.cfg";
}

// Matches femm/GeneralPrefs.cpp's own tag detection: case-insensitive
// prefix compare against "<Tag>", value after the closing '>' (which may
// be followed by whitespace and/or '=' before the actual value -- classic
// FEMM's own StripKey()/sscanf("%i", ...) skips past both, so this does
// too rather than requiring an exact "= " separator).
bool matchesTag(const QString& line, const char* tag, QString* value)
{
  QString t = QString::fromLatin1(tag);
  QString trimmed = line.trimmed();
  if (!trimmed.startsWith(t, Qt::CaseInsensitive))
    return false;
  if (value) {
    QString rest = trimmed.mid(t.length()).trimmed();
    if (rest.startsWith('='))
      rest = rest.mid(1).trimmed();
    *value = rest;
  }
  return true;
}

} // namespace

AppPreferences AppPreferences::load()
{
  AppPreferences prefs;

  QFile file(cfgPath());
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return prefs;

  QTextStream in(&file);
  while (!in.atEnd()) {
    QString line = in.readLine();
    QString value;
    if (matchesTag(line, "<ShowConsole>", &value))
      prefs.showConsole = value.toInt() != 0;
    else if (matchesTag(line, "<SeparatePlots>", &value))
      prefs.separatePlots = value.toInt() != 0;
    else if (matchesTag(line, "<ShowOutputWindow>", &value))
      prefs.showOutputWindow = value.toInt() != 0;
    else if (matchesTag(line, "<SmartMesh>", &value))
      prefs.smartMesh = value.toInt() != 0;
    else if (matchesTag(line, "<DefaultType>", &value))
      prefs.defaultDocType = value.toInt();
    else if (matchesTag(line, "<QtDarkTheme>", &value))
      prefs.darkTheme = value.toInt() != 0;
  }
  return prefs;
}

bool AppPreferences::save() const
{
  QStringList lines;
  bool haveShowConsole = false, haveSeparatePlots = false, haveShowOutputWindow = false;
  bool haveSmartMesh = false, haveDefaultType = false, haveDarkTheme = false;

  QFile readFile(cfgPath());
  if (readFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&readFile);
    while (!in.atEnd()) {
      QString line = in.readLine();
      if (matchesTag(line, "<ShowConsole>", nullptr)) {
        lines << QStringLiteral("<ShowConsole>      = %1").arg(showConsole ? 1 : 0);
        haveShowConsole = true;
      } else if (matchesTag(line, "<SeparatePlots>", nullptr)) {
        lines << QStringLiteral("<SeparatePlots>    = %1").arg(separatePlots ? 1 : 0);
        haveSeparatePlots = true;
      } else if (matchesTag(line, "<ShowOutputWindow>", nullptr)) {
        lines << QStringLiteral("<ShowOutputWindow> = %1").arg(showOutputWindow ? 1 : 0);
        haveShowOutputWindow = true;
      } else if (matchesTag(line, "<SmartMesh>", nullptr)) {
        lines << QStringLiteral("<SmartMesh>        = %1").arg(smartMesh ? 1 : 0);
        haveSmartMesh = true;
      } else if (matchesTag(line, "<DefaultType>", nullptr)) {
        lines << QStringLiteral("<DefaultType>      = %1").arg(defaultDocType);
        haveDefaultType = true;
      } else if (matchesTag(line, "<QtDarkTheme>", nullptr)) {
        lines << QStringLiteral("<QtDarkTheme>      = %1").arg(darkTheme ? 1 : 0);
        haveDarkTheme = true;
      } else {
        lines << line;
      }
    }
    readFile.close();
  }

  if (!haveShowConsole)
    lines << QStringLiteral("<ShowConsole>      = %1").arg(showConsole ? 1 : 0);
  if (!haveSeparatePlots)
    lines << QStringLiteral("<SeparatePlots>    = %1").arg(separatePlots ? 1 : 0);
  if (!haveShowOutputWindow)
    lines << QStringLiteral("<ShowOutputWindow> = %1").arg(showOutputWindow ? 1 : 0);
  if (!haveSmartMesh)
    lines << QStringLiteral("<SmartMesh>        = %1").arg(smartMesh ? 1 : 0);
  if (!haveDefaultType)
    lines << QStringLiteral("<DefaultType>      = %1").arg(defaultDocType);
  if (!haveDarkTheme)
    lines << QStringLiteral("<QtDarkTheme>      = %1").arg(darkTheme ? 1 : 0);

  QFile writeFile(cfgPath());
  if (!writeFile.open(QIODevice::WriteOnly | QIODevice::Text))
    return false;
  QTextStream out(&writeFile);
  for (const QString& line : lines)
    out << line << "\n";
  return true;
}
