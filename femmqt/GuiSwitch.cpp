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
  // Defaults to Qt when femm.cfg has no <PreferredGUI> key yet (fresh
  // install, or a femm.cfg predating this key) -- the Qt GUI is the
  // default GUI (see script.nsi's FEMMX.lnk Start Menu shortcut, which
  // now launches femmqt.exe). Nothing currently calls this to redirect
  // femmx.exe's own startup (that would mean touching CFemmApp::
  // InitInstance, which also handles COM automation -- out of scope,
  // too much risk of affecting existing pyfemm/Octave/Scilab automation
  // for this phase); it's read only where a caller explicitly wants to
  // know the user's last explicit choice.
  QFile file(cfgPath());
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return PreferredGui::Qt;

  QTextStream in(&file);
  while (!in.atEnd()) {
    QString line = in.readLine();
    QString value;
    if (matchesTag(line, "<PreferredGUI>", &value))
      return (value.toInt() != 0) ? PreferredGui::Qt : PreferredGui::Classic;
  }
  return PreferredGui::Qt;
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
