#include "GuiSwitch.h"

#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QTextStream>

namespace {

QString cfgPath()
{
  return QCoreApplication::applicationDirPath() + "/femm.cfg";
}

// Matches femm/GeneralPrefs.cpp's own tag-detection: case-insensitive
// prefix compare against "<Tag>", value after the closing '>'.
bool matchesTag(const QString& line, const char* tag, QString* value)
{
  QString t = QString::fromLatin1(tag);
  QString trimmed = line.trimmed();
  if (!trimmed.startsWith(t, Qt::CaseInsensitive))
    return false;
  if (value) {
    int eq = trimmed.indexOf('=');
    *value = (eq >= 0) ? trimmed.mid(eq + 1).trimmed() : QString();
  }
  return true;
}

} // namespace

GuiSwitch::PreferredGui GuiSwitch::readPreferredGui()
{
  // Defaults to Classic when femm.cfg has no <PreferredGUI> key yet (a
  // fresh install, or a femm.cfg predating the key).
  //
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-11:
  // this used to default to Qt, on the stated grounds that femmqt.exe was
  // what script.nsi's FEMMX.lnk launched. That was reverted in v2.0.x --
  // FEMMX.lnk points at femmx.exe again -- and the comment was left
  // behind, so the two readers of this one key disagreed about what "no
  // key" means: femm/ScriptGui.cpp's ReadPreferredGuiFromCfg() answers
  // Classic and this answered Qt. Nothing in femmqt calls this yet, so
  // the disagreement was invisible; the first caller would have inherited
  // it. Both now answer Classic (issue #19).
  //
  // Nothing currently calls this to redirect femmx.exe's own startup
  // (that would mean touching CFemmApp::InitInstance, which also handles
  // COM automation -- too much risk to existing pyfemm/Octave/Scilab
  // automation); it's read only where a caller explicitly wants to know
  // the user's last explicit choice.
  QFile file(cfgPath());
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return PreferredGui::Classic;

  QTextStream in(&file);
  while (!in.atEnd()) {
    QString line = in.readLine();
    QString value;
    if (matchesTag(line, "<PreferredGUI>", &value))
      return (value.toInt() != 0) ? PreferredGui::Qt : PreferredGui::Classic;
  }
  return PreferredGui::Classic;
}

bool GuiSwitch::writePreferredGui(PreferredGui value)
{
  QStringList lines;
  bool replaced = false;

  QFile readFile(cfgPath());
  if (readFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&readFile);
    while (!in.atEnd()) {
      QString line = in.readLine();
      if (matchesTag(line, "<PreferredGUI>", nullptr)) {
        lines << QStringLiteral("<PreferredGUI>    = %1").arg((int)value);
        replaced = true;
      } else {
        lines << line;
      }
    }
    readFile.close();
  }
  if (!replaced)
    lines << QStringLiteral("<PreferredGUI>    = %1").arg((int)value);

  QFile writeFile(cfgPath());
  if (!writeFile.open(QIODevice::WriteOnly | QIODevice::Text))
    return false;
  QTextStream out(&writeFile);
  for (const QString& line : lines)
    out << line << "\n";
  return true;
}

bool GuiSwitch::launchClassicGui(const QString& filePath)
{
  QString exe = QCoreApplication::applicationDirPath() + "/femmx.exe";
  if (!QFile::exists(exe))
    return false;
  QStringList args;
  if (!filePath.isEmpty())
    args << filePath;
  return QProcess::startDetached(exe, args);
}
