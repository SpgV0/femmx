#pragma once

#include <QString>

// Reads/writes the <PreferredGUI> key in femm.cfg (the flat "<Tag> =
// value" file both GUIs read from/write to next to their own exe --
// see femm/GeneralPrefs.cpp's CGeneralPrefs::ScanPrefs/WritePrefs for the
// existing format and the 5 keys the classic GUI already keeps there),
// and launches the other GUI's executable.
//
// Known limitation, not fixed here (out of scope -- would mean touching
// femm/GeneralPrefs.cpp): CGeneralPrefs::WritePrefs() unconditionally
// rewrites all 5 keys IT knows about every time the classic GUI's
// Preferences dialog is saved, but has no notion of <PreferredGUI> --
// so saving Preferences in the classic GUI after this has set
// <PreferredGUI> will silently drop it back to the classic GUI's
// default. This module's own writePreferredGui() preserves every other
// line it doesn't recognize when it rewrites the file, so the reverse
// (Qt GUI clobbering the classic GUI's preferences) doesn't happen.
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
