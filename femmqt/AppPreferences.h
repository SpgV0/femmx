#pragma once

// Reads/writes femm.cfg's General Preferences fields (femm/GeneralPrefs.cpp's
// CGeneralPrefs::ScanPrefs/WritePrefs -- <ShowConsole>/<SeparatePlots>/
// <ShowOutputWindow>/<SmartMesh>/<DefaultType>) plus a Qt-only
// <QtDarkTheme> extension key the classic GUI doesn't know about. Like
// GuiSwitch.h's <PreferredGUI> handling, save() preserves every line it
// doesn't recognize when rewriting the file, so this and GuiSwitch don't
// clobber each other. Known asymmetry (see GuiSwitch.h's own comment,
// same root cause): the classic GUI's own WritePrefs() unconditionally
// rewrites femm.cfg with only ITS 5 keys, so saving Preferences there
// after this has set <QtDarkTheme>/<PreferredGUI> will silently drop them
// back to defaults. Not fixable without touching femm/GeneralPrefs.cpp,
// which is out of scope here.
struct AppPreferences {
  // Lua console at startup -- stored/round-tripped only; femmqt has no Lua
  // console yet (see the plan's Lua Console scope note).
  bool showConsole = false; // <ShowConsole>
  bool separatePlots = false; // <SeparatePlots>
  bool showOutputWindow = true; // <ShowOutputWindow>
  // Default for NEW problems only -- an already-open FemmProblem's own
  // smartMesh field (set per-file in Problem Properties) always wins.
  bool smartMesh = true; // <SmartMesh>
  // Index into classic FEMM's New-Document-type combo (magnetics/
  // electrostatics/heat flow/current flow). femmqt is magnetics-only, so
  // this has no effect here -- round-tripped unchanged so it isn't lost
  // for the classic GUI.
  int defaultDocType = 0; // <DefaultType>
  bool darkTheme = false; // <QtDarkTheme>, Qt-only

  static AppPreferences load();
  bool save() const;
};
