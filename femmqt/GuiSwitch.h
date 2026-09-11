#pragma once

#include <QString>

// Reads/writes the <PreferredGUI> key in femm.cfg (the flat "<Tag> =
// value" file both GUIs read from/write to next to their own exe --
// see femm/GeneralPrefs.cpp's CGeneralPrefs::ScanPrefs/WritePrefs for the
// existing format and the 5 keys the classic GUI already keeps there),
// and launches the other GUI's executable.
//
// Both writers preserve what they don't recognize. writePreferredGui()
// below rewrites only the <PreferredGUI> line and keeps every other line
// intact, and CGeneralPrefs::WritePrefs() does the same in the other
// direction. It did not always: until issue #19 it truncated femm.cfg and
// wrote back only the 5 keys that dialog knows about, so saving
// Preferences in the classic GUI silently reset <PreferredGUI> (and threw
// away the Qt GUI's <QtDarkTheme>).
namespace GuiSwitch {

enum class PreferredGui {
  Classic = 0,
  Qt = 1,
};

PreferredGui readPreferredGui();

// Preserves every line in the existing femm.cfg it doesn't recognize
// (i.e. all 5 of the classic GUI's own keys), replacing or appending
// only the <PreferredGUI> line.
bool writePreferredGui(PreferredGui value);

// Launches the classic GUI (femmx.exe, expected next to femmqt.exe --
// both ship flat in the same bin\ directory, see script.nsi) with
// `filePath` as its command-line argument (may be empty for "no file").
// Returns false if femmx.exe couldn't be found/started.
bool launchClassicGui(const QString& filePath);

}
