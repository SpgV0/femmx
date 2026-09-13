#include "stdafx.h"

#include "ScriptGui.h"

#include <gdiplus.h>
#include <stdio.h>

#pragma comment(lib, "gdiplus.lib")

namespace {

BOOL g_seeded = FALSE;
ScriptGui g_gui = ScriptGui::Classic;

// Mirrors how CFemmeView::OnSwitchToQtGui reads/writes the same key --
// femm.cfg is a flat "<Tag> = value" file (see GeneralPrefs.cpp) and
// <PreferredGUI> is 0 for the classic GUI, 1 for Qt.
ScriptGui ReadPreferredGuiFromCfg()
{
  ScriptGui result = ScriptGui::Classic;

  // BinDir isn't reachable from here (it's a member of four different
  // Doc/View classes, not a global), so locate femm.cfg the same way
  // those classes build BinDir in the first place: next to this exe.
  char exePath[MAX_PATH] = { 0 };
  if (GetModuleFileName(NULL, exePath, MAX_PATH) == 0)
    return result;
  char* lastSlash = strrchr(exePath, '\\');
  if (lastSlash == NULL)
    return result;
  *(lastSlash + 1) = '\0';

  CString cfg;
  cfg.Format("%sfemm.cfg", exePath);

  FILE* fp = fopen(cfg, "rt");
  if (fp == NULL)
    return result;

  char line[1024];
  while (fgets(line, sizeof(line), fp) != NULL) {
    CString s(line);
    s.TrimLeft();
    if (_strnicmp(s, "<PreferredGUI>", 14) != 0)
      continue;
    int eq = s.Find('=');
    if (eq < 0)
      continue;
    CString value = s.Mid(eq + 1);
    value.TrimLeft();
    if (atoi(value) == 1)
      result = ScriptGui::Qt;
    break;
  }
  fclose(fp);
  return result;
}

int GetEncoderClsid(const WCHAR* mimeType, CLSID* pClsid)
{
  UINT num = 0, size = 0;
  Gdiplus::GetImageEncodersSize(&num, &size);
  if (size == 0)
    return -1;

  Gdiplus::ImageCodecInfo* info = (Gdiplus::ImageCodecInfo*)malloc(size);
  if (info == NULL)
    return -1;

  Gdiplus::GetImageEncoders(num, size, info);
  int found = -1;
  for (UINT j = 0; j < num; j++) {
    if (wcscmp(info[j].MimeType, mimeType) == 0) {
      *pClsid = info[j].Clsid;
      found = (int)j;
      break;
    }
  }
  free(info);
  return found;
}

} // namespace

ScriptGui GetScriptGui()
{
  if (!g_seeded) {
    g_gui = ReadPreferredGuiFromCfg();
    g_seeded = TRUE;
  }
  return g_gui;
}

void SetScriptGui(ScriptGui g)
{
  g_gui = g;
  g_seeded = TRUE;
}

BOOL ParseScriptGui(const char* name, ScriptGui* out)
{
  if (name == NULL || out == NULL)
    return FALSE;

  CString s(name);
  s.TrimLeft();
  s.TrimRight();
  s.MakeLower();

  if (s == "classic" || s == "old" || s == "femm" || s == "mfc" || s == "0") {
    *out = ScriptGui::Classic;
    return TRUE;
  }
  if (s == "qt" || s == "new" || s == "femmqt" || s == "1") {
    *out = ScriptGui::Qt;
    return TRUE;
  }
  return FALSE;
}

const char* ScriptGuiName(ScriptGui g)
{
  return (g == ScriptGui::Qt) ? "qt" : "classic";
}

BOOL SetPreferredGuiInCfg(const char* binDir, ScriptGui gui)
{
  if (binDir == NULL)
    return FALSE;

  CString fname;
  fname.Format("%sfemm.cfg", binDir);

  CStringArray lines;
  BOOL bReplaced = FALSE;
  CString newLine;
  newLine.Format("<PreferredGUI>    = %d", (gui == ScriptGui::Qt) ? 1 : 0);

  FILE* fp = fopen(fname, "rt");
  if (fp != NULL) {
    char s[1024];
    while (fgets(s, 1024, fp) != NULL) {
      CString line(s);
      line.TrimRight("\r\n");
      CString trimmed = line;
      trimmed.TrimLeft();
      if (_strnicmp(trimmed, "<PreferredGUI>", 14) == 0) {
        lines.Add(newLine);
        bReplaced = TRUE;
      } else {
        lines.Add(line);
      }
    }
    fclose(fp);
  }
  if (!bReplaced)
    lines.Add(newLine);

  fp = fopen(fname, "wt");
  if (fp == NULL)
    return FALSE;
  for (int i = 0; i < lines.GetSize(); i++)
    fprintf(fp, "%s\n", (const char*)lines[i]);
  fclose(fp);
  return TRUE;
}

BOOL HandOffToQtGui(const char* binDir, const char* docPath, CString* errOut)
{
  if (binDir == NULL || docPath == NULL || *docPath == '\0') {
    if (errOut)
      *errOut = "no file on disk to hand off to the Qt GUI";
    return FALSE;
  }

  CString exe;
  exe.Format("%sfemmqt.exe", binDir);
  if (GetFileAttributes(exe) == INVALID_FILE_ATTRIBUTES) {
    if (errOut)
      errOut->Format("Couldn't find femmqt.exe next to femm.exe (looked for %s).",
          (const char*)exe);
    return FALSE;
  }

  // Written before the process starts, so that if starting it fails the
  // preference is still what the user asked for -- and, more
  // importantly, so femmqt is never racing a write to the file it may
  // read on startup.
  SetPreferredGuiInCfg(binDir, ScriptGui::Qt);

  CString cmd;
  cmd.Format("\"%s\" \"%s\"", (const char*)exe, docPath);

  STARTUPINFO si = { 0 };
  PROCESS_INFORMATION pi;
  si.cb = sizeof(STARTUPINFO);
  if (!CreateProcess(NULL, cmd.GetBuffer(0), NULL, NULL, FALSE, 0, NULL, NULL,
          &si, &pi)) {
    cmd.ReleaseBuffer();
    if (errOut)
      *errOut = "Couldn't start femmqt.exe.";
    return FALSE;
  }
  cmd.ReleaseBuffer();

  // Deliberately NOT waited for, unlike RenderPngViaQtGui: this is a
  // handoff, and this process is about to close.
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return TRUE;
}

BOOL SaveHBitmapAsPng(HBITMAP hBmp, const char* pngPath)
{
  if (hBmp == NULL || pngPath == NULL)
    return FALSE;

  Gdiplus::GdiplusStartupInput startupInput;
  ULONG_PTR token = 0;
  if (Gdiplus::GdiplusStartup(&token, &startupInput, NULL) != Gdiplus::Ok)
    return FALSE;

  BOOL ok = FALSE;
  {
    // Scoped so the Bitmap is destroyed before GdiplusShutdown -- GDI+
    // objects must not outlive the token that created them.
    Gdiplus::Bitmap bitmap(hBmp, NULL);
    CLSID clsid;
    if (bitmap.GetLastStatus() == Gdiplus::Ok
        && GetEncoderClsid(L"image/png", &clsid) >= 0) {
      int wlen = MultiByteToWideChar(CP_ACP, 0, pngPath, -1, NULL, 0);
      if (wlen > 0) {
        WCHAR* wpath = (WCHAR*)malloc(wlen * sizeof(WCHAR));
        if (wpath != NULL) {
          MultiByteToWideChar(CP_ACP, 0, pngPath, -1, wpath, wlen);
          ok = (bitmap.Save(wpath, &clsid, NULL) == Gdiplus::Ok);
          free(wpath);
        }
      }
    }
  }

  Gdiplus::GdiplusShutdown(token);
  return ok;
}

CString QtDensityQuantityName(int densityPlot, double frequency)
{
  if (densityPlot <= 0)
    return CString("");

  // Mirrors femm/femmviewLua.cpp's lua_showdensity, which builds this
  // same index from these same names -- read backwards.
  if (frequency == 0) {
    switch (densityPlot) {
    case 1:
      return CString("bmag");
    case 2:
      return CString("hmag");
    case 3:
      return CString("jmag");
    case 4:
      return CString("logb");
    default:
      return CString("");
    }
  }

  static const char* const kAc[10] = { "bmag", "breal", "bimag", "hmag", "hreal",
    "himag", "jmag", "jreal", "jimag", "logb" };
  if (densityPlot >= 1 && densityPlot <= 10)
    return CString(kAc[densityPlot - 1]);
  return CString("");
}

BOOL QtDensityPlotIsRenderable(QtRenderPhysics physics, int densityPlot,
    CString* wanted, CString* renderable)
{
  // The names are the ones each post-processor prints on its own
  // legend (belaviewView.cpp, hviewView.cpp, cviewView.cpp), so a
  // refusal names the quantity using the same words the window does.
  static const char* const kEs[] = { "V, Volts", "|D|, C/m^2", "|E|, V/m" };
  static const char* const kHt[] = { "Temperature, K", "|F|, W/m^2", "|G|, K/m" };
  static const char* const kCf[] = { "|V|, Volts", "|Re(V)|, Volts",
    "|Im(V)|, Volts", "|J|, A/m^2", "|Re(J)|, A/m^2", "|Im(J)|, A/m^2",
    "|E|, V/m", "|Re(E)|, V/m", "|Im(E)|, V/m" };

  const char* const* names = kEs;
  int count = 3;
  int renderableIndex = 2; // 1-based, matching DensityPlot

  switch (physics) {
  case QtRenderPhysics::Electrostatics:
    names = kEs;
    count = 3;
    renderableIndex = 2; // |D|
    break;
  case QtRenderPhysics::HeatFlow:
    names = kHt;
    count = 3;
    renderableIndex = 2; // |F|
    break;
  case QtRenderPhysics::CurrentFlow:
    names = kCf;
    count = 9;
    renderableIndex = 4; // |J|
    break;
  }

  if (renderable)
    *renderable = CString(names[renderableIndex - 1]);

  // 0 means the density plot is off -- a contour plot, which the Qt
  // viewer draws for every physics.
  if (densityPlot <= 0)
    return TRUE;
  if (densityPlot == renderableIndex)
    return TRUE;

  if (wanted) {
    if (densityPlot <= count)
      *wanted = CString(names[densityPlot - 1]);
    else
      wanted->Format("density plot %d", densityPlot);
  }
  return FALSE;
}

BOOL RenderPngViaQtGui(const char* binDir, const char* docPath,
    const char* pngPath, int width, int height, const QtPlotState* plot,
    CString* errOut)
{
  CString exe;
  exe.Format("%sfemmqt.exe", binDir);
  if (GetFileAttributes(exe) == INVALID_FILE_ATTRIBUTES) {
    if (errOut)
      errOut->Format("femmqt.exe not found next to femm.exe (looked for %s)",
          (const char*)exe);
    return FALSE;
  }

  CString cmd;
  cmd.Format("\"%s\" --render-png \"%s\" \"%s\" %d %d",
      (const char*)exe, docPath, pngPath, width, height);

  // Issue #86: the view state the script configured. The crop is
  // positional and must stay immediately after the size; the named
  // options follow it.
  if (plot != NULL && plot->haveState) {
    if (plot->haveCrop) {
      CString crop;
      crop.Format(" %.10g %.10g %.10g %.10g", plot->x0, plot->y0, plot->x1,
          plot->y1);
      cmd += crop;
    }

    cmd += plot->density ? " --density" : " --contour";

    if (plot->quantity.GetLength() > 0) {
      CString q;
      q.Format(" --quantity %s", (const char*)plot->quantity);
      cmd += q;
    }
    if (plot->haveBounds) {
      CString b;
      b.Format(" --bounds %.10g %.10g", plot->lower, plot->upper);
      cmd += b;
    }
    CString flags;
    flags.Format(" --greyscale %d --legend %d", plot->greyscale ? 1 : 0,
        plot->legend ? 1 : 0);
    cmd += flags;
    if (plot->numContours > 0) {
      CString c;
      c.Format(" --contours %d", plot->numContours);
      cmd += c;
    }
  }

  STARTUPINFO si = { 0 };
  PROCESS_INFORMATION pi;
  si.cb = sizeof(STARTUPINFO);
  if (!CreateProcess(NULL, cmd.GetBuffer(0), NULL, NULL, FALSE, 0, NULL, NULL,
          &si, &pi)) {
    cmd.ReleaseBuffer();
    if (errOut)
      *errOut = "couldn't start femmqt.exe";
    return FALSE;
  }
  cmd.ReleaseBuffer();

  // Wait, unlike the View > Switch To Qt GUI menu item which deliberately
  // hands off and exits: a script's next line may well open the PNG this
  // call was supposed to produce.
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exitCode = 1;
  GetExitCodeProcess(pi.hProcess, &exitCode);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  if (exitCode != 0) {
    if (errOut)
      errOut->Format("femmqt.exe --render-png failed (exit code %lu)", exitCode);
    return FALSE;
  }
  return TRUE;
}
