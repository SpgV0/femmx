// DarkMode.h : app-wide dark mode support (VS Code Dark+-style palette)
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-07-17:
// extends the existing per-view canvas dark theme (CFemmeView::ApplyTheme
// and friends, which only ever recolored the current editor's own GDI
// drawing) to the rest of the application's UI chrome: every top-level
// window's title bar (main frame, and every dialog -- there's no shared
// dialog base class in this codebase, so dialogs are caught via a
// thread-scoped WH_CBT hook rather than by editing each of the ~100+
// dialog .cpp files individually), toolbars, menus, and native controls
// (buttons, edit boxes, list/combo boxes, static text, scrollbars).
//
// Uses public, documented Win32 APIs (DwmSetWindowAttribute,
// SetWindowTheme, SetWindowSubclass) plus a handful of long-standing
// *undocumented* uxtheme.dll ordinal exports (SetPreferredAppMode,
// AllowDarkModeForWindow, FlushMenuThemes, RefreshImmersiveColorPolicy-
// State) that many mainstream apps rely on for this exact purpose (e.g.
// Windows Terminal, various Microsoft/JetBrains tools), since Microsoft
// has never shipped a public "make my classic Win32 app's native
// controls dark" API. Loaded via LoadLibrary/GetProcAddress by ordinal
// and guarded throughout -- a future Windows build that removes or
// renumbers them just leaves the app light-themed instead of crashing.
#pragma once

namespace DarkMode {

// VS Code's "Dark+" palette.
extern const COLORREF kEditorBg; // #1e1e1e -- canvas / primary content background
extern const COLORREF kPanelBg; // #252526 -- dialogs, static/group areas
extern const COLORREF kControlBg; // #3c3c3c -- edit boxes, list/combo boxes
extern const COLORREF kBorder; // #3c3c3c -- borders/separators
extern const COLORREF kSelectionBg; // #264f78 -- selected list/tree item
extern const COLORREF kText; // #d4d4d4 -- primary text
extern const COLORREF kTextMuted; // #808080 -- disabled/secondary text
extern const COLORREF kAccent; // #007acc -- links, focus, progress

// Call once from CFemmApp::InitInstance(), before creating any windows.
void Init();

// Current global state -- single source of truth for the whole app,
// unlike the older per-view m_bDarkTheme flags (each of which only ever
// tracked and recolored its own view's canvas).
BOOL IsEnabled();

// Flips the global flag and re-themes every currently open top-level
// window (main frame, any open dialogs) and their descendants (MDI
// children, toolbars, and all child controls). Windows created
// afterwards pick up the current state automatically via the CBT hook
// installed by Init().
void SetEnabled(BOOL bEnable);

// Applies (or removes, if dark mode is currently off) title bar + native
// control theming to a single window and all its descendants. Called
// automatically by the CBT hook for newly created top-level windows and
// by SetEnabled() for ones that already existed when the mode was
// toggled; exposed here too in case a caller needs to re-theme a window
// right after creating it itself.
void ApplyToWindowTree(HWND hwnd);

} // namespace DarkMode
