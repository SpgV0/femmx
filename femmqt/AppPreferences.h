#pragma once

#include "SnapEngine.h"

// Reads/writes femm.cfg's General Preferences fields (femm/GeneralPrefs.cpp's
// CGeneralPrefs::ScanPrefs/WritePrefs -- <ShowConsole>/<SeparatePlots>/
// <ShowOutputWindow>/<SmartMesh>/<DefaultType>) plus a Qt-only
// <QtDarkTheme> extension key the classic GUI doesn't know about. Like
// GuiSwitch.h's <PreferredGUI> handling, save() preserves every line it
// doesn't recognize when rewriting the file, so this and GuiSwitch don't
// clobber each other. The classic GUI's WritePrefs() now preserves
// unrecognized lines as well (issue #19) -- until then it truncated
// femm.cfg and wrote back only ITS 5 keys, so saving Preferences there
// silently reset <QtDarkTheme> and <PreferredGUI>.
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
  // Which object-snap types are enabled (issue #28), as a
  // SnapEngine::SnapFlags bitmask. One integer rather than nine tags:
  // femm.cfg is shared with the classic GUI, which has no object
  // snapping at all, and nine Qt-only lines in a file the other program
  // also reads and rewrites is a poor trade for readability nobody
  // benefits from. Qt-only, like <QtDarkTheme>.
  unsigned snapFlags = SnapEngine::SnapDefault; // <QtSnapFlags>

  static AppPreferences load();
  bool save() const;
};
