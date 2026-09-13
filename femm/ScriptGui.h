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

// The post-processor view state that crosses to femmqt.
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #86). femmqt is a separate process: it opens the solution
// fresh, with its own defaults, and can see nothing the script
// configured here. Before this, the shell-out passed a size and
// nothing else, so
//
//     mo_showdensityplot(1, 0, 2.0, 0, "bmag")
//     mo_savepng("out.png")
//
// under setgui("qt") produced femmqt's default CONTOUR plot -- 59% of
// pixels different from what was asked for, and no error to say so.
//
// WHAT DOES NOT CROSS, and why it is listed rather than quietly
// dropped:
//
//   Vector plot   femmqt has no vector overlay at all -- it was removed
//                 at the user's request. mo_showvectorplot has no
//                 effect on a Qt render.
//   Contours      the mask/Re-vs-Im contour selection (ShowAr/ShowAi/
//                 ShowMask) has no femmqt equivalent yet; the contour
//                 COUNT and bounds do cross.
//   Smoothing,    femmqt has its own settings for these and they are
//   points, mesh  not part of what mo_savepng asks for.
//
// See manual_qt for the same list in prose.
struct QtPlotState {
  // Nothing set: render with femmqt's own defaults. This is what the
  // pre-processor's mi_savepng passes, having no plot state at all.
  BOOL haveState;

  BOOL density;       // FALSE renders contour lines
  CString quantity;   // "bmag".."logb", the mo_showdensityplot vocabulary
  BOOL haveBounds;
  double lower, upper;
  BOOL greyscale;
  BOOL legend;
  int numContours;    // 0 leaves femmqt's default

  // The visible region in model coordinates. It matters for more than
  // framing: the density plot's colour banding is scaled to what is
  // VISIBLE, so a full-model render cannot reproduce a zoomed-in view's
  // colours.
  BOOL haveCrop;
  double x0, y0, x1, y1;

  QtPlotState()
      : haveState(FALSE), density(FALSE), haveBounds(FALSE), lower(0), upper(0),
        greyscale(FALSE), legend(TRUE), numContours(0), haveCrop(FALSE), x0(0),
        y0(0), x1(0), y1(0)
  {
  }
};

// Turns the classic post-processor's DensityPlot index into the name
// femmqt's --quantity expects. The index means DIFFERENT THINGS at DC
// and AC -- 2 is Re(B) in a harmonic problem and |H| in a static one --
// so the frequency is required, and passing the raw integer across
// would be silently wrong for half of all problems. Returns an empty
// string for 0 (density plot off) or an index out of range.
CString QtDensityQuantityName(int densityPlot, double frequency);

// Renders an already-saved document to PNG by shelling out to
// femmqt.exe --render-png, and WAITS for it. binDir must end in a
// separator (the CFemmeView/CFemmviewView BinDir convention). plot may
// be NULL, meaning "femmqt's defaults". errOut, if given, receives a
// human-readable reason on failure.
BOOL RenderPngViaQtGui(const char* binDir, const char* docPath,
    const char* pngPath, int width, int height, const QtPlotState* plot,
    CString* errOut);
