#pragma once

// Which GUI a script's GUI-mediated commands should use.
//
// Added per direct user request: "can you make a new lua command to
// choose between old and new gui when scripting, taking pngs etc."
//
// Lua only ever runs inside the classic MFC app (femmqt.exe embeds no
// interpreter), so this is not a switch of which process the script runs
// in. It selects which GUI *renders* for the commands that produce
// GUI-derived output -- today that is mi_savepng/mo_savepng, which is
// what makes the choice visible: the classic view draws its own plot
// styling, while femmqt draws its density/contour/vector plots with
// antialiasing and its own palette.
//
// Session-scoped ON PURPOSE. It is seeded from the <PreferredGUI> key in
// femm.cfg, so a script with no setgui() call renders with whichever GUI
// the user actually prefers, but setgui() itself does NOT write that key
// back. A script asking to render one image a particular way should not
// silently repoint which GUI the user's Start Menu shortcut opens --
// that is what the GUI's own View > Switch To... menu item is for.

#include <afx.h>
#include <windows.h>

enum class ScriptGui {
  Classic = 0, // femmx.exe, the MFC GUI -- also the femm.cfg default
  Qt = 1,      // femmqt.exe
};

// Seeded from femm.cfg's <PreferredGUI> on first use, then whatever
// setgui() last asked for.
ScriptGui GetScriptGui();
void SetScriptGui(ScriptGui g);

// Accepts "classic"/"old"/"femm"/"mfc"/"0" and "qt"/"new"/"femmqt"/"1",
// case-insensitively. Returns false if the name matches neither, so the
// caller can report the bad argument rather than silently defaulting.
BOOL ParseScriptGui(const char* name, ScriptGui* out);
const char* ScriptGuiName(ScriptGui g);

// Encodes an HBITMAP to PNG through GDI+, which the app already links
// for the load monitor (see LoadMonitorDlg.cpp's own GetEncoderClsid).
// Starts and shuts down GDI+ around the call rather than assuming a
// process-wide token: nothing initialises one at startup today.
BOOL SaveHBitmapAsPng(HBITMAP hBmp, const char* pngPath);

// Renders an already-saved document to PNG by shelling out to
// femmqt.exe --render-png, and WAITS for it. binDir must end in a
// separator (the CFemmeView/CFemmviewView BinDir convention). errOut, if
// given, receives a human-readable reason on failure.
BOOL RenderPngViaQtGui(const char* binDir, const char* docPath,
    const char* pngPath, int width, int height, CString* errOut);
