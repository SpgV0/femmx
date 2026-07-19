---
name: femmqt-qt-gui
description: "New Qt6-based GUI (femmqt/) built alongside the classic MFC GUI: magnetics-only Phase 1, full property editing, and a menu-by-menu parity pass (zoom/pan/grid/undo/move/copy/scale/mirror, mesh split, post-processor analysis tools) -- what it covers, what it doesn't, and where the design decisions live"
metadata:
  type: project
  originSessionId: 846a52dc-e5cc-4b0f-9a4f-7b5debeae297
  modified: 2026-07-19T12:21:05.431Z
---

Built a second GUI for FEMMX, `femmqt/` (Qt6.11.1, MSVC kit at
`C:\Qt\6.11.1\msvc2022_64`), alongside the existing MFC one
(`femm/`/`femmx.exe`), per the user's request to modernize geometry
creation and solution plotting while keeping the classic GUI available.
Committed 2026-07-19 as `68e088c` on `new_features` (pushed).

**Scope: magnetics only (Phase 1).** Electrostatics/heat/current-flow are
untouched -- the classic GUI is still the only option for those. Full
design/implementation plan (including what's explicitly deferred) is at
`C:\Users\spgry\.claude\plans\quizzical-chasing-kernighan.md` -- read that
before starting Phase 2 (the other 3 physics types) or extending Phase 1.

**What it does**: `.fem` geometry editor (`QGraphicsScene`-based,
interactive add/move/delete of nodes/segments/arcs/block labels), drives
`triangle.exe` -> `fkn.exe` to solve (reimplements the `.poly`/`.pbc` mesh
step independently of `femm/writepoly.cpp`, verified byte-identical
output), and a density-plot solution viewer reading `.ans`.

**Two new binary cache formats**, both designed the same way (magic +
versioned header, flat POD record arrays, bulk read/write, staleness
checked via source file size+mtime, silently regenerated when stale):
- `.ansx` -- caches a solved `.ans`'s mesh with flux density precomputed
  once. ~80x faster to reopen than re-parsing the source `.ans`.
- `.femx` -- same idea for `.fem` geometry (added when the user asked
  for a `.fem`-side equivalent after seeing `.ansx`'s speedup).
Neither is a new "source of truth" format -- `.fem`/`.ans` stay canonical
and portable; the `x` variants are pure, regenerable performance caches,
each Qt-app-private (`femmx.exe`/`fkn.exe`/`triangle.exe` never read or
write them).

**GUI switching**: a "Switch to Qt/Classic GUI" menu item in *each* GUI
(added to the classic GUI's `IDR_FEMMETYPE`/`IDR_FEMMVIEWTYPE` menus too,
`femm/FemmeView.cpp`/`FemmviewView.cpp`'s `OnSwitchToQtGui` -- the first
change to `femm/` this whole effort made) writes `<PreferredGUI>` to
`femm.cfg` and hands the current file to the other executable via
`CreateProcess`/`QProcess::startDetached`, then closes. Verified working
in both directions. femmqt.exe is now the **default** GUI (`script.nsi`'s
primary Start Menu shortcut launches it; classic GUI has its own
`FEMMX (Classic).lnk`). **COM automation still targets `femmx.exe`
unchanged** -- femmqt.exe has no COM automation support, so pyfemm/
Octave/Mathematica/Scilab scripts are unaffected by any of this.

**Real bugs found and fixed during implementation** (worth knowing about
if touching this code): `.fem`'s bare closing tags (`<EndBdry>` etc. have
no `=`) need a special check before the tag-dispatch `continue`, or a
parser silently swallows the rest of the file (see FemmFileIO.cpp);
`QGraphicsItem`s need `ItemIgnoresTransformations` + explicit `setZValue`
for node handles to stay clickable/on-top regardless of zoom or z-order;
`QGraphicsScene` needs an explicit large fixed `setSceneRect()` or the
view silently repans between edits, breaking click-to-scene-coordinate
mapping; Qt's default double-click handling swallows the second press of
two same-position clicks, so tool-click handling must be duplicated into
`mouseDoubleClickEvent` too; populating a scene via `setPos()` on fresh
items fires the same `itemChange`/edited-signal path as a real drag, so
`m_dirty = false` must be set *after* `setProblem()`, not before.

**Build system notes**: `femmqt/CMakeLists.txt` is fully independent of
`femm/`'s (own `CMAKE_CXX_STANDARD 17`, since `femm/` has none and relies
on MSVC's legacy mode -- see [[build_and_com_registration_gotchas]] for
the related `<chrono>` finding). No 32-bit Qt6 kit exists, so `build.ps1`
now passes `-DSKIP_femmqt:BOOL=ON` on its 32-bit-only passes (both the
`-ForceTriangle32bit` secondary pass and the plain full-32-bit path).
`build_femmx.ps1`'s plain/cuda variant-move step now also moves Qt's
plugin subdirectories (`platforms/`, `styles/`, etc.), not just top-level
files, using a named allowlist rather than "any directory in bin\" --
learned the hard way after a blanket-move first draft relocated an
unrelated pre-existing stray folder.

See also [[push_branch_policy]] (updated same day: now push to
`new_features` once a day as a work backup, not just when told).

**Update (2026-07-19, same day): full property editing added, committed
`e2cad69`.** Problem Properties dialog; Material/Boundary/Circuit/
PointProp libraries via a shared `PropertyListDialog` (Add/Duplicate/
Edit/Delete callbacks) + a per-type field editor, under a new Problem
menu; per-entity (node/segment/arc/block-label) dialogs on double-click
in Select mode; an Add Arc tool; index-renumbering helpers
(`FemmProblemEdit::delete*Prop`) so deleting a still-referenced property
resets/renumbers cleanly instead of corrupting indices. Toolbar is now
icon-only (`femmqt/toolbar_icons/*.svg`), recolored at runtime from
`QPalette::ButtonText` (`IconTheme.cpp`) so it's correct in both light
and dark modes without shipping two asset sets.

**Real bug found and fixed**: `FemmNode`'s third `.fem` column was
modeled as a `boundaryProps` reference (matching Segment/Arc's own
`boundaryMarker`) but the classic GUI's writer (`femm/FemmeDoc.cpp:
2596-2601`) actually resolves it against `nodeproplist`/`pointProps` --
individual nodes only ever carry a *point* property in classic FEMM,
never a standalone boundary condition. Renamed to `pointPropIndex`
everywhere (FemmProblem.h, FemmFileIO.cpp, FemxFileIO.cpp,
MeshBuilder.cpp). Round-tripping was already correct by accident (the
raw integer just passed through untouched, nothing dereferenced it), so
this stayed latent until building the point-property assignment UI
required getting the semantics right. Worth checking for if this data
model is extended further.

**Other gotchas found this round**: `qt_standard_project_setup()` does
NOT reliably enable `CMAKE_AUTORCC` -- a `.qrc` listed in
`qt_add_executable()`'s sources silently never got compiled until
`set(CMAKE_AUTORCC ON)` was added explicitly before the `qt_add_executable`
call. `.gitignore`'s standalone `Icon?` line (for macOS's cursed
`Icon\r` custom-folder-icon files) collides case-insensitively with any
5-letter directory named `icons` on Windows git (`core.ignorecase`) --
new icon assets went in `femmqt/toolbar_icons/` instead, not
`femmqt/icons/`, to dodge it (don't touch the `Icon?` rule itself, it's
legitimate elsewhere). A `.qrc`'s resource path is `prefix + "/" +
alias` (default alias = the `<file>` text verbatim) -- `<file>icons/
foo.svg</file>` under `prefix="/icons"` resolves to `:/icons/icons/
foo.svg`, not `:/icons/foo.svg`; use an explicit `alias="foo.svg"` to
get the shorter runtime path.

**Update (2026-07-19, later same day): menu-by-menu parity pass,
committed on `new_features`.** User asked directly whether every classic-
GUI button had been covered -- it hadn't (checked `femm.rc`'s actual menu
resources for the real gap list). Added: geometry-editor Zoom/Pan/Grid,
single-level Undo (snapshot-before-destructive-op, matching classic's own
`UpdateUndo`/`Undo`), Move/Copy/Scale/Mirror on selection, Show Block
Names, Mesh menu split (Create/Show/Purge, separate from Solve --
`SolveRunner::mesh()`/`MeshOverlay.h`), Show Orphans, simplified Group
mode, Help/Recent Files; post-processor Contour/Vector plot modes
alongside Density (`MeshSolutionItem::PlotMode`), Point/Contour/Area
analysis tools, Plot X-Y (text table, no charting lib linked), Problem
Info. Full details and exact scope trims (Create Radius/Open Boundary/
Materials Library/Preferences/Dark Theme toggle/DXF/Print/Lua Console all
deliberately deferred, documented) are in the plan doc's "Round 3"
section -- read that before extending any of this further. Contour/
Vector plots verified against a real solved file (straight wire) with
textbook-correct output (concentric equipotential circles; tangential
right-hand-rule B field) -- high confidence. Point/Area tools share the
same verified interpolation code but weren't independently click-tested
(see the UI-automation note below -- the host was in active concurrent
use during this round, confirmed via `win32gui.GetForegroundWindow()`
changing mid-script, so heavy interactive automation was deliberately
scaled back in favor of code review + build verification).

**`.qrc` resource paths and `CMAKE_AUTORCC` gotchas** (from the toolbar
icons work earlier the same day): a `.qrc`'s runtime resource path is
`prefix + "/" + alias` where alias defaults to the `<file>` element's
text verbatim -- `<file>icons/foo.svg</file>` under `prefix="/icons"`
resolves to `:/icons/icons/foo.svg`, not the shorter path you'd expect;
use an explicit `alias="foo.svg"` to get `:/icons/foo.svg`. Separately,
`qt_standard_project_setup()` does NOT reliably enable `CMAKE_AUTORCC` --
a `.qrc` listed in `qt_add_executable()`'s sources silently never got
compiled into a `qrc_*.cpp` until `set(CMAKE_AUTORCC ON)` was added
explicitly before the call, and the runtime symptom (`QFile::open(":/...")`
failing) doesn't point at the build system at all, easy to chase in the
wrong place. Also: this repo's `.gitignore` has a standalone `Icon?` line
(macOS Finder's cursed `Icon\r` custom-folder-icon convention) that
collides case-insensitively with any 5-letter directory literally named
`icons` on Windows git (`core.ignorecase`) -- new icon assets live in
`femmqt/toolbar_icons/`, not `femmqt/icons/`, to dodge this; don't touch
the `Icon?` rule itself, it's legitimate elsewhere.

**Double-click UI-automation gotcha (test methodology, not an app bug)**:
`pywinauto.mouse.double_click()` / `click_input(double=True)` did not
reproduce a genuine Qt double-click in this environment no matter the
timing -- confirmed via a temporary `mouseDoubleClickEvent` stderr trace
that Qt never received a coalesced double-click event, only two separate
single presses. A raw `ctypes`/`user32.SendInput`-style sequence
(`SetCursorPos` + `mouse_event(LEFTDOWN)` + 20ms + `mouse_event(LEFTUP)`,
~50ms gap, repeat) reliably registers as a real double-click. Reach for
this first next time double-click automation is needed against this app
rather than re-discovering it.
