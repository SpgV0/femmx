// DarkMode.cpp : app-wide dark mode support -- see DarkMode.h.
#include "stdafx.h"
#include "DarkMode.h"
#include <dwmapi.h>
#include <uxtheme.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace DarkMode {

const COLORREF kEditorBg = RGB(0x1e, 0x1e, 0x1e);
const COLORREF kPanelBg = RGB(0x25, 0x25, 0x26);
const COLORREF kControlBg = RGB(0x3c, 0x3c, 0x3c);
const COLORREF kBorder = RGB(0x3c, 0x3c, 0x3c);
const COLORREF kSelectionBg = RGB(0x26, 0x4f, 0x78);
const COLORREF kText = RGB(0xd4, 0xd4, 0xd4);
const COLORREF kTextMuted = RGB(0x80, 0x80, 0x80);
const COLORREF kAccent = RGB(0x00, 0x7a, 0xcc);

namespace {

const UINT_PTR kSubclassId = 1;

BOOL g_bEnabled = FALSE;
HHOOK g_hCbtHook = NULL;
HBRUSH g_hbrPanel = NULL;
HBRUSH g_hbrControl = NULL;

// Undocumented uxtheme.dll ordinal exports -- see the docstring in
// DarkMode.h for why these are needed at all and why calls through them
// are guarded (null function pointer if GetProcAddress fails).
typedef BOOL(WINAPI* AllowDarkModeForWindowFn)(HWND, BOOL);
typedef VOID(WINAPI* RefreshImmersiveColorPolicyStateFn)();
typedef VOID(WINAPI* FlushMenuThemesFn)();
typedef BOOL(WINAPI* SetPreferredAppModeFn)(int);

AllowDarkModeForWindowFn pAllowDarkModeForWindow = NULL;
RefreshImmersiveColorPolicyStateFn pRefreshImmersiveColorPolicyState = NULL;
FlushMenuThemesFn pFlushMenuThemes = NULL;
SetPreferredAppModeFn pSetPreferredAppMode = NULL;

void LoadUxThemeOrdinals()
{
  HMODULE hUxtheme = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!hUxtheme)
    return;
  pAllowDarkModeForWindow = (AllowDarkModeForWindowFn)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133));
  pFlushMenuThemes = (FlushMenuThemesFn)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(136));
  pRefreshImmersiveColorPolicyState = (RefreshImmersiveColorPolicyStateFn)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(104));
  pSetPreferredAppMode = (SetPreferredAppModeFn)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
}

// Subclasses every themed window (installed by ApplyToOneWindow below) so
// its own background and its native controls' WM_CTLCOLOR* replies match
// the palette -- SetWindowTheme's "DarkMode_Explorer" theme alone mainly
// darkens scrollbars/hover glow/borders, not control text/background
// colors, which classic (non-owner-drawn) common controls still ask
// their parent for via these messages.
LRESULT CALLBACK DarkSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
{
  if (g_bEnabled) {
    switch (msg) {
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
      HDC hdc = (HDC)wParam;
      SetTextColor(hdc, kText);
      SetBkColor(hdc, kControlBg);
      return (LRESULT)g_hbrControl;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG: {
      HDC hdc = (HDC)wParam;
      SetTextColor(hdc, kText);
      SetBkColor(hdc, kPanelBg);
      SetBkMode(hdc, TRANSPARENT);
      return (LRESULT)g_hbrPanel;
    }
    case WM_ERASEBKGND: {
      RECT rc;
      GetClientRect(hwnd, &rc);
      FillRect((HDC)wParam, &rc, g_hbrPanel);
      return 1;
    }
    }
  }
  if (msg == WM_NCDESTROY) {
    LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
    RemoveWindowSubclass(hwnd, DarkSubclassProc, kSubclassId);
    return r;
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

BOOL CALLBACK ApplyToChildProc(HWND hwnd, LPARAM lParam)
{
  BOOL dark = (BOOL)lParam;
  SetWindowTheme(hwnd, dark ? L"DarkMode_Explorer" : NULL, NULL);
  if (pAllowDarkModeForWindow)
    pAllowDarkModeForWindow(hwnd, dark);
  if (dark)
    SetWindowSubclass(hwnd, DarkSubclassProc, kSubclassId, 0);
  else
    RemoveWindowSubclass(hwnd, DarkSubclassProc, kSubclassId);
  return TRUE;
}

void ApplyToOneWindow(HWND hwnd, BOOL dark)
{
  if (!::IsWindow(hwnd))
    return;
  BOOL useDark = dark;
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
  ApplyToChildProc(hwnd, (LPARAM)dark); // theme + subclass the window itself too
  EnumChildWindows(hwnd, ApplyToChildProc, (LPARAM)dark);
  // Two separate "now repaint" nudges are needed for a window that was
  // already visible when the mode was toggled (as opposed to one just
  // being created, which picks up both naturally on its first paint):
  // SWP_FRAMECHANGED forces DWM to redo the *non-client* area (title
  // bar) -- DwmSetWindowAttribute alone does not trigger this on its
  // own -- and RedrawWindow forces the *client* area (this window's own
  // background plus every subclassed child's) to repaint immediately.
  SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
      SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  RedrawWindow(hwnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

LRESULT CALLBACK CbtProc(int nCode, WPARAM wParam, LPARAM lParam)
{
  // HCBT_CREATEWND, not HCBT_ACTIVATE: this fires once per window as it
  // is created -- including each individual child control of a dialog,
  // one at a time -- so theming each window right here catches every
  // control regardless of when it was created relative to the dialog's
  // own activation. HCBT_ACTIVATE (tried first) fires only once per
  // *dialog*, at a point where most of its child controls (created
  // during/after WM_INITDIALOG) don't exist yet -- confirmed via tracing:
  // EnumChildWindows found only 2 of a 7-control About box at that point.
  if (nCode == HCBT_CREATEWND && g_bEnabled) {
    HWND hwnd = (HWND)wParam;
    LPCBT_CREATEWND pcbt = (LPCBT_CREATEWND)lParam;
    BOOL isChild = (pcbt->lpcs->style & WS_CHILD) != 0;
    ApplyToChildProc(hwnd, (LPARAM)TRUE); // theme + subclass, same treatment for child or top-level
    if (!isChild) {
      BOOL useDark = TRUE;
      DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
    }
  }
  return CallNextHookEx(g_hCbtHook, nCode, wParam, lParam);
}

BOOL CALLBACK EnumTopLevelProc(HWND hwnd, LPARAM lParam)
{
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == GetCurrentProcessId())
    ApplyToOneWindow(hwnd, (BOOL)lParam);
  return TRUE;
}

} // namespace

void Init()
{
  LoadUxThemeOrdinals();
  if (pSetPreferredAppMode)
    pSetPreferredAppMode(1 /* AllowDark */);
  g_hbrPanel = CreateSolidBrush(kPanelBg);
  g_hbrControl = CreateSolidBrush(kControlBg);
  g_hCbtHook = SetWindowsHookEx(WH_CBT, CbtProc, NULL, GetCurrentThreadId());
}

BOOL IsEnabled()
{
  return g_bEnabled;
}

void SetEnabled(BOOL bEnable)
{
  if (bEnable == g_bEnabled)
    return;
  g_bEnabled = bEnable;
  if (pRefreshImmersiveColorPolicyState)
    pRefreshImmersiveColorPolicyState();
  if (pFlushMenuThemes)
    pFlushMenuThemes();
  // Re-theme every top-level window this process already owns (dialogs,
  // the main frame -- whose EnumChildWindows pass below also reaches
  // every MDI child, since those are true child windows of the main
  // frame's MDICLIENT). Windows created after this call pick up the new
  // state automatically via the CBT hook.
  EnumWindows(EnumTopLevelProc, (LPARAM)bEnable);
}

void ApplyToWindowTree(HWND hwnd)
{
  ApplyToOneWindow(hwnd, g_bEnabled);
}

} // namespace DarkMode
