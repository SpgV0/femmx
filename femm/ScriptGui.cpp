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

BOOL RenderPngViaQtGui(const char* binDir, const char* docPath,
    const char* pngPath, int width, int height, CString* errOut)
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
