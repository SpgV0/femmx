---
name: femmqt-qt-gui
description: "New Qt6-based GUI (femmqt/) built alongside the classic MFC GUI: magnetics-only Phase 1, full property editing, and a menu-by-menu parity pass (zoom/pan/grid/undo/move/copy/scale/mirror, mesh split, post-processor analysis tools) -- what it covers, what it doesn't, and where the design decisions live"
metadata:
  type: project
  originSessionId: 846a52dc-e5cc-4b0f-9a4f-7b5debeae297
  modified: 2026-07-20T11:13:44.619Z
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

**Update (2026-07-19, later still): user asked directly whether Round 3
covered the analysis (post-processor) window -- it hadn't.** Checking
`femm.rc`'s `IDR_FEMMVIEWTYPE` menu directly (rather than trusting my own
earlier summary) showed Zoom/Edit/several View items had only been added
to the geometry editor, not the Solution Viewer. Also caught a dead stub
in my own prior work: `MeshSolutionItem::setSmoothing()` was wired to a
member no paint method ever read. Filled in: Zoom menu, Copy as Bitmap,
real Smoothing (bands on node-averaged |B| -- `QPainter` has no native
triangle-gradient primitive so this approximates true Gouraud shading,
see `m_nodeBMagAvg`), Show Mesh/Points overlays, Status Bar toggle (added
to both windows -- missing from the geometry editor too), Reload, Recent
Files (shares `MainWindow`'s `QSettings` key), Switch to Classic GUI,
Help Topics/License, Integrate. Full accounting of what's still missing
(Circuit Props/BH Curves/Print/Preferences/Dark Theme/Lua Console/Output
Window) is in the plan doc's "Round 4" section.

**Real bug found via my own testing of this round's work**: zooming in
past ~8x on a real solved mesh showed black gaps between triangles in
the density plot. Root-caused (by toggling Smoothing on/off -- no
change -- and reloading fresh at low zoom -- clean) to `QPainter::
Antialiasing` being off (a pre-existing large-mesh-performance choice)
combined with triangles shrinking to a few screen pixels at high zoom --
a case only newly reachable because this round added zoom controls that
didn't exist before. Fixed with a scale-dependent AA toggle
(`SolutionGraphicsView::updateAntialiasingForScale()`, threshold 8x)
called after every zoom-changing operation. Worth remembering: adding a
new capability (zoom) can expose a latent issue in *unrelated*,
already-shipped code that was fine under the old, narrower reachable
state space -- re-test old features after adding new ways to reach them,
not just the new feature itself.

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

**Round 5 (2026-07-19, "implement these too"): the rest of Round 3/4's
deferred list, committed `c9f7e8d` on `new_features` (pushed).** User
explicitly rejected an `Agent`/subagent tool call for this round's
research -- all of it was done via direct `Read`/`Grep`, a hard
constraint for any further work on this codebase unless told otherwise.
Implemented, each a faithful port of the classic algorithm (not an
approximation) unless noted:
- **Create Radius**: all 3 cases (two segments/two arcs/one of each)
  ported from `femm/FemmeDoc.cpp`'s `CreateRadius` -- the tangent-circle
  geometry (up to 8 candidate solutions via `std::complex`, filtered by
  which actually touch both original entities) is in
  `FemmProblemEdit::createRadius`.
- **Create Open Boundary (ABC)**: `bin/init.lua`'s `mi_makeABC`, including
  the exact `u2D0`/`u2D1`/`uAx0`/`uAx1` permeability tables (n=1..12),
  transcribed verbatim into `OpenBoundaryDialog.cpp`.
- **Materials Library**: `MaterialLibraryIO` parses `bin/matlib.dat`'s
  `<BeginFolder>`/`<BeginBlock>` tree (same tags as `.fem`'s own
  `BlockProps`) into a browsable dialog.
- **DXF import/export** (`DxfIO.h/.cpp`): ports `femm/MOVECOPY.CPP`'s
  `ReadDXF`/`WriteDXF` line-for-line (POINT/LINE/ARC/CIRCLE/LWPOLYLINE/
  POLYLINE+VERTEX, bulge-factor arc splitting). One deliberate
  simplification, documented in the header: node merging is coincident-
  point dedup only, not classic's full `FancyEnforcePSLG`
  intersection-splitting -- a DXF with genuinely crossing, non-coincident
  geometry imports with those crossings unresolved.
- **Preferences + Dark Theme**: `AppPreferences` mirrors `GuiSwitch`'s
  tag-preserving femm.cfg read/write (the classic GUI's 5 keys +
  `<QtDarkTheme>`). `AppTheme` drives both `QApplication::setPalette()`
  and named scene colors that `GeometryScene`/`SolutionView` now read
  instead of hardcoded `Qt::black` etc. -- toggling calls `refreshTheme()`
  (a full `rebuild()`) since item colors are baked in at creation, not
  read live per-paint. Verified live: canvas, menus, and toolbar icons all
  repaint correctly on toggle.
- **CPU/GPU/RAM Load Monitor**: Qt-painted port of
  `femm/LoadMonitorDlg` (GetSystemTimes/NVML-dynamic-load/
  GlobalMemoryStatusEx same as the original). Exposed a real architecture
  gap while wiring it in: `SolveRunner::solve()`/`mesh()` used to call
  `QProcess::waitForFinished(-1)`, which blocks the event loop entirely --
  fine on its own, but it meant the Load Monitor's `QTimer` could never
  fire during a solve. Fixed by polling in short bursts and pumping
  `QCoreApplication::processEvents()` between them; `MainWindow` now
  disables its menu bar for the duration (that pumping makes a second
  concurrent solve/mesh reachable, which isn't safe -- both would write
  the same `.poly`/`.pbc`/`.ans` files).
- **Output Window**: a persistent dock in the Solution Viewer echoing
  every Point/Contour/Area result (classic FEMM only ever shows the
  *last* one in its docked `IDC_OUTBOX`; keeping a scrollback is strictly
  more useful and no harder).
- **Exterior Region dialog**, **Print/Print Preview** (Qt's built-in
  `QPrintDialog`/`QPrintPreviewDialog`), **Copy as Bitmap** for the
  geometry editor.

**Still deliberately deferred, with reasons** (not silently dropped):
Circuit Props and BH Curves need per-element J/sigma data (a
`GetJA`-equivalent current density calc, multi-turn `LocalEnergy`
correction, `PlnInt`/`AxiInt` integration) that `.ans`/`.ansx`/
`MeshSolutionElement` don't currently carry at all -- confirmed by
reading `femm/CircDlg.cpp` and `femm/FemmviewDoc.cpp`'s
`GetVoltageDrop`/`GetFluxLinkage` in full; this is a real numerical
feature on the order of the original `.ans` reader itself, not a
menu-item-sized task. Lua Console remains out of proportion
(`femmeLua.cpp` is thousands of lines). Copy as Metafile still skipped
(no clean cross-platform EMF path in Qt).

**Verification this round**: full local rebuild via `build_qt` (direct
`cmake --build . --target femmqt`, not the full `build.ps1`, for faster
iteration -- caught two real compile errors this way: `windows.h`'s
`max`/`min` macros clobbering `std::max`/`std::min` in
`LoadMonitorDialog.cpp`, needing `#define NOMINMAX` before the
`#include <windows.h>`). Then live-tested: launched the built exe,
force-foregrounded it (the established `keybd_event(VK_MENU)` +
`SetForegroundWindow` trick), and screenshotted the Edit/Problem/View
menus (all new items present), the Dark Theme toggle (canvas + toolbar
icons repainted correctly), the Preferences dialog, and the Load Monitor
dialog (GPU trace detected via NVML on this machine) -- all opened and
rendered correctly with no crash. Note for next time: pywinauto's
`Application.windows()`/`app.window(title=...)` did NOT enumerate a
QDialog as a top-level window in this environment (returned only the
main window) even though it was genuinely open and visible -- had to
fall back to raw `win32gui.EnumWindows` + `win32process.
GetWindowThreadProcessId` to find its real HWND, then drive it via
`win32gui`/`win32api` directly (or `app.window(handle=...)` once the
HWND is known) instead of pywinauto's own window-finding.

**Round 6 (2026-07-19, same day, "continue with the rest"): Circuit
Props and BH Curve editing, committed `5cc2ca5` on `new_features`
(pushed).** User pushed past Round 5's Circuit Props/BH Curves deferral;
delivered both. Lua Console stays deferred -- and got a second, sharper
reason while investigating: `liblua/CMakeLists.txt` calls
`find_package(MFC REQUIRED)` (`CMAKE_MFC_FLAG 2`), so femmqt (which
deliberately avoids MFC -- see this file's very first paragraph) can't
link the existing liblua target as-is; embedding Lua would mean building
a second, non-MFC variant of it first, on top of the ~200-function
`femmeLua.cpp` binding surface already flagged as its own project.

**BH Curve editing** (`BHCurveDialog.h/.cpp`): straightforward --
`FemmMaterialProp::bhData` was already read/written by `FemmFileIO.cpp`,
just not exposed for editing. A `QTableWidget` (B, H columns) plus a
Qt-painted linear/log chart, wired into `MaterialPropDialog` via a new
"Edit BH Curve..." button. Verified live: added rows, typed in a real
saturation-curve-shaped point set via raw coordinate clicks (pywinauto
couldn't address `QTableWidgetItem`s as `Edit` controls until a cell is
double-clicked into edit mode -- raw `win32api.mouse_event` double-click
at the cell's screen coordinates worked instead), and the chart rendered
the correct curve shape.

**Circuit Props** (`CircuitAnalysis.h/.cpp`) -- the one worth reading
carefully before touching again. Ported `femm/FemmviewDoc.cpp`'s
`GetVoltageDrop`/`GetFluxLinkage`/`GetJA`, solid-conductor branches only
(stranded/litz needs `GetFillFactor`'s frequency-dependent curve fits,
not ported; zero-current circuits need a separate mutual-inductance
cascade, not ported -- both rejected with a clear `Result::error` rather
than guessed at). The real story is the validation method, not the
formulas themselves:

1. First pass: derived the solid-conductor voltage/current relationship
   from first principles (Kirchhoff's current law over the block, solving
   for the unknown terminal voltage) rather than trusting a literal
   source port, out of concern that classic FEMM's own length-unit
   conventions (`LengthConv[LengthUnits]`) might not be applied
   consistently across the functions involved.
2. That derivation was WRONG -- off by exactly the length-conversion
   factor (1000x for a millimeters file) -- caught only by an empirical
   check: opened the *same* real solved file
   (`test/results/straight_wire_field/straight_wire_field.ans` -- a 10A
   DC single-solid-conductor series circuit, currently untracked/
   uncommitted local test data, not yet in git) in the classic GUI
   (`bin/plain/femmx.exe`) via the same UI-automation approach used
   throughout this session, read its actual Circuit Properties dialog
   output (Voltage Drop = 5.48984e-05 V, Flux Linkage = 1.1653e-08 Wb),
   and cross-checked a standalone Python re-implementation against it.
3. Root cause: `Depth` (planar) DOES need `LengthConv` to meters in the
   final `Volts = -Depth*dVolts` formula, but the raw `dVolts` value read
   directly from `.ans`'s block-label circuit-info section (right after
   the node/element lists in `[Solution]`, written by
   `fkn/prob1big.cpp`'s `WriteStatic2D`) is used as-is, NOT further
   converted -- an inconsistency-looking mix that's exactly what the real
   source does, and exactly what an "obviously correct" reasoned
   re-derivation got wrong.
4. After fixing, re-verified BOTH the standalone Python calc AND the
   actual built Qt app's Circuit Properties dialog against the same
   file -- byte-identical to the classic GUI's displayed values (Total
   current, Voltage Drop, Flux Linkage, Flux/Current, Voltage/Current,
   Power all matched).

**Round 7 (2026-07-19, same day, "skip the lua console, make sure
everything else is added... show coordinates when drawing... and the
value of the field when hovering"), committed `65df00f` on
`new_features` (pushed).** Two explicitly-requested live status-bar
readouts, plus a fresh femm.rc audit now that the big deferred-list items
were done (found real remaining gaps: Print Setup was missing in both
windows, Print/Print Preview/Print Setup were missing *entirely* from
the Solution Viewer, Delete/Open Selected had no menu items -- keyboard/
double-click only -- and the Solution Viewer had no read-only "BH
Curves" viewer distinct from the geometry editor's editable one).
- **Mouse-position readout** (geometry editor): `GeometryScene::
  mousePositionChanged` emitted from `mouseMoveEvent` (grid-snapped),
  driving a permanent status-bar `QLabel` in `MainWindow`. Needed
  `GeometryView::setMouseTracking(true)` -- without it, `QGraphicsScene::
  mouseMoveEvent` only fires while a button is held (dragging), not on
  plain hover.
- **Field-value-on-hover readout** (Solution Viewer): `SolutionGraphicsView::
  hoveredAt`, reusing the Point tool's own `findContainingElement`/
  `interpolateA`. Throttled to ~20 updates/sec (`QElapsedTimer`) --
  `findContainingElement` is a documented linear scan over every mesh
  element, fine for one deliberate click but not for firing on every
  pixel of mouse movement across a multi-million-element mesh.
- Verified live for both: screenshotted the status bar mid-hover over the
  straight-wire test file -- showed live x/y plus `|B|`/`A` matching the
  known-correct field pattern at that point.

**Round 8 (2026-07-19, same day, 3 direct fixes: dark-theme text
contrast, cursor-following coordinates "like Fusion 360", and toolbar
icons), committed `f191e1a` on `new_features` (pushed).**
- **Dark theme text-field bug, found and fixed**: `QLineEdit`/
  `QComboBox` backgrounds stayed a light native gray with barely-visible
  near-white text in every dialog, even though the custom `QPalette` was
  applied and OTHER widgets (labels, checkboxes) went dark correctly.
  Root cause, confirmed by screenshotting Problem Properties before/
  after: Windows' native styles (windowsvista/windows11) render those
  two controls from OS theme chrome rather than fully honoring a custom
  `QPalette::Base`. Fix: `AppTheme::setDark(true)` now also switches to
  the Fusion style (Qt's own recommended style for custom palettes --
  the ONLY style that reliably honors every palette role), restoring
  whatever native style was active at startup on light mode. **Lesson
  for any future Qt dark-mode work on Windows**: a custom `QPalette`
  alone is not sufficient for full theming on native Windows styles --
  pair it with `QStyleFactory::create("Fusion")` or expect exactly this
  kind of silently-wrong-contrast bug in text-entry widgets specifically.
- **Cursor-following coordinate/value readout** ("like Fusion 360"): a
  floating `QLabel` parented to the viewport, repositioned on every
  mouse move (offset down-right of the cursor, clamped to the viewport
  edges) -- `GeometryView` shows the live model-space coordinate while
  drawing, `SolutionGraphicsView` shows coordinate + interpolated `|B|`/
  `A`. Additive to the fixed status-bar readouts added in Round 7, not a
  replacement -- both now show the same live text.
- **Toolbar icons**: searched `femm.rc`'s actual `TOOLBAR` resource
  blocks (`IDR_FEMMETYPE`, `IDR_LEFTBAR`, `IDR_FEMMVIEWTYPE`) for the
  authoritative icon list per direct user request ("list all the icons
  used... by searching in the old GUI") rather than guessing -- found
  the Solution Viewer had **no toolbar at all** (text menus only) and
  the geometry editor only had its original 5 drawing-tool icons.
  ~32 new SVGs added to `toolbar_icons/` (same `CURRENTCOLOR`-
  placeholder convention as the existing 5). Wired into 4 new toolbars
  total: geometry editor gets Edit (Undo/Open Selected/Delete/Move/Copy/
  Scale/Mirror/Create Radius/Create Open Boundary/Select by Group), Mesh
  (Create Mesh/Solve/View Results), and a left-docked Navigate toolbar
  (Zoom/Pan/Grid, matching `femm.rc`'s own separate `IDR_LEFTBAR`
  layout); Solution Viewer gets one Operation toolbar (Point/Contours/
  Areas/Plot X-Y/Integrate/Circuit Props/Show Mesh/Contour/Density/
  Vector Plot). Checkable actions reuse the exact same `QAction` objects
  their menu items already created (not copies) so toolbar buttons and
  menu checkmarks never drift apart -- `MainWindow`/`SolutionWindow` each
  got an `addThemedAction()` helper that remembers every new action's
  icon path in `m_themedActions` so `refreshToolbarIcons()` re-tints all
  of them (not just the original 5) after a dark/light toggle.
- Verified live: screenshotted both windows' toolbars in light and dark
  mode (icons correctly re-tint), the floating tooltips tracking the
  cursor with live text in both windows, and the fixed dark-theme dialog
  fields (white-on-dark, confirmed before/after the Fusion-style fix).

**Round 9 (2026-07-19, same day, 3 more direct requests: 2-second hover
help text on every icon, "make sure all icons have the implemented
functionality and test it", and boundary-condition edge coloring),
committed `1accc3c` on `new_features` (pushed).**
- **2-second hover tooltips** (`HoverTooltip.h/.cpp`): Qt's own tooltip
  appears almost instantly (~700ms) -- this suppresses that (`QEvent::
  ToolTip` swallowed in an event filter) and shows the same text itself
  via `QToolTip::showText()` from an `Enter`-triggered 2000ms `QTimer`
  instead. Every toolbar action in both windows got an explicit
  `setToolTip()` -- text pulled from `femm.rc`'s own `STRINGTABLE`
  where a description exists for that command ID (confirmed most core
  edit/mesh/zoom/pan/grid commands DO have one there), written fresh for
  Qt-only commands (Dark Theme, Load Monitor, Circuit Props, plot
  modes) that have no classic equivalent. Verified by screenshotting a
  button at 0.8s (nothing) and 2.2s (tooltip visible, correct text).
- **Comprehensive click-through verification**: opened a real test file
  and clicked every single toolbar button in both windows, checking for
  a REAL effect (dialog opened with sane content, geometry actually
  changed, files actually written), not just "didn't crash". Caught and
  fixed a genuine TEST-SCRIPT bug worth remembering for next time: using
  `win32gui.GetWindowRect` (full window incl. title bar) instead of the
  client area for click coordinates silently misaligned every click by
  the title-bar height -- fixed by using `win32gui.ClientToScreen(hwnd,
  (0,0))` as the coordinate origin instead. Also hit a **modal-dialog-
  eats-later-clicks** trap: a `QDialog::exec()`-opened dialog left open
  by an earlier test step silently absorbed every subsequent synthetic
  click in the same script (no crash, no visible effect) -- looked
  exactly like "button does nothing" until checking for a stray already-
  open window. Confirmed genuinely working, with real evidence, not just
  a click: Create Mesh (fresh `.node`/`.ele`/`.poly`/`.pbc` files with
  matching timestamps), Zoom In (5 clicks visibly revealed individual
  mesh triangles), Zoom Window (a real multi-step rubber-band drag
  correctly reframed the view -- a single-jump `SetCursorPos` drag
  without intermediate move steps did NOT register with Qt, worth
  remembering: synthetic drags need multiple intermediate `SetCursorPos`
  calls, not just press-at-A/release-at-B), Delete+Undo (node removed,
  then restored), Solve (produced a real solved `.ans`, auto-opened the
  Solution Viewer), and in the Solution Viewer: Point/Contours/Areas
  (each produced correct results, all three echoed to the Output
  Window), Circuit Props (reproduced this session's earlier-validated
  reference numbers exactly, again), Density/Contour/Vector plot mode
  (Vector mode visibly showed real directional arrows).
- **Boundary-condition edge coloring** (`AppTheme::boundaryEdgeColor()`,
  wired into `GeometryScene::addSegmentItem`/`addArcItem`): a segment or
  arc with `boundaryMarker != 0` now renders in a distinct orange shade
  instead of its normal segment/arc color -- classic FEMM has no
  equivalent, purely a modern-CAD-style affordance. Free-riding on
  already-existing behavior: `MainWindow::onEntityDoubleClicked` already
  unconditionally calls `m_scene->rebuild()` after any accepted property
  edit, so this recolors automatically the moment a boundary condition
  is assigned/cleared via the properties dialog -- no new refresh logic
  needed. Verified live against a file with a real Dirichlet ("A=0")
  boundary on its outer arc -- rendered orange as expected.

**Lesson, worth remembering beyond this one feature**: when a classic-
FEMM formula involves `LengthConv`/unit conversions, do not trust a
"this looks dimensionally sensible" re-derivation over a literal port --
this codebase's internal unit conventions are inconsistent enough
(confirmed directly: the same physical quantity is converted in one call
site and left raw in another, apparently deliberately, not a typo) that
only two things are reliable: (a) a literal, line-by-line port of the
actual source, or (b) empirical validation against the classic GUI's own
output on a real file. Prefer having both, as this round ended up doing.
The `bin/plain/femmx.exe` + UI-automation-screenshot technique (already
established this session for other verification) is directly reusable as
an oracle for any future post-processing/numerical feature ported from
`femm/FemmviewDoc.cpp` -- worth reaching for immediately rather than
trusting a from-scratch derivation, given this session's experience.

## Round 10: v2.0.0 -- Qt becomes the sole Start Menu entry, a real MFC bug found via user report (2026-07-19/20)

Committed `d2eb7c8` on `new_features` (pushed). User reported "switch to
qt gui did not work for me" / "the application crashed" after launching
"FEMMX (Classic)" from the Start Menu and using its Solution Viewer's
"Switch to Qt GUI..." menu item.

**Real bug, confirmed by direct reproduction, not the user's environment**:
`femm/FemmviewView.cpp`'s `CFemmviewView` (the classic GUI's post-
processor) had a fully-implemented `OnSwitchToQtGui()` handler and a
correctly-declared header/menu-resource entry, but the
`ON_COMMAND(ID_VIEW_SWITCHTOQT, OnSwitchToQtGui)` line was simply
missing from its `BEGIN_MESSAGE_MAP`/`END_MESSAGE_MAP` block -- clicking
the menu item silently did nothing (not a crash; "did not work" was the
accurate half of the report). `femm/FemmeView.cpp` (the pre-processor/
editor)'s copy of the same feature *was* wired correctly, which is why
reproducing against the editor first showed nothing wrong -- only
testing the post-processor specifically (`.ans` file, not `.fem`)
surfaced it. **Reproduction technique**: launch `femmx.exe <file>` as a
real subprocess, find its real window via `win32gui.EnumWindows` +
`win32process.GetWindowThreadProcessId` filtered to that PID (there can
be more than one top-level window -- e.g. a modeless "FEMM Output"
dialog, class `#32770`, alongside the real main frame; grabbing the
first enumerated match without checking the title is a trap), then
`win32gui.PostMessage(hwnd, win32con.WM_COMMAND, <resource.h ID>, 0)` --
exactly reproduces a real menu click for MFC's command routing (verified
`GetMenu`/`GetSubMenu`/`GetMenuItemID` walk to confirm the ID is really
in that window's menu tree first), and poll `proc.poll()` / `Get-Process
-Id ... | Responding` afterward to distinguish "silently did nothing"
(still alive, responsive, no effect) from a real crash/hang. Fix: one
added `ON_COMMAND` line.

**Per user request, also removed the "FEMMX (Classic)" Start Menu
shortcut** (`script.nsi`) -- a user following it out of habit is exactly
who hit the bug above. `femmx.exe` stays fully installed (still load-
bearing: `femm.ActiveFEMM` COM automation, and the only option for
electrostatics/heat-flow/current-flow, which femmqt doesn't support
yet), just no longer pinned to the Start Menu -- reachable via
`bin\femmx.exe` directly or the Qt GUI's own "Switch to Classic GUI".
`FEMMX.lnk` now launches `femmqt.exe` exclusively. This is a real
behavior change other users of the installer will notice.

**Version bumped to 2.0.0** (major, not patch -- marks Qt becoming the
default a fresh install actually launches): `femm/femm.rc`'s
`FEMMX_VERSION_MAJOR`/`MINOR` macros + the 2 hardcoded strings
(`IDD_ABOUTBOX`, `IDR_MAINFRAME` STRINGTABLE) that can't use the macro
(rc.exe rejects it there, RC2116/RC2108 -- established in the v1.2.0
round), `script.nsi`'s `PROJECT_VERSION`, `CHANGELOG.md`. **Not tagged**
-- version bump only, per explicit instruction; tagging/merge-to-main is
a separate, not-yet-done step (see [[release_tagging_workflow]]).

**Found while touching versioning: `manual/manual.tex` is gitignored**
(`.gitignore:38`, pre-existing, not something this session added) -- so
the v1.2.0-era changelog's claim of updating the manual's title page
never actually landed (found it still hardcoded "1.1.1" from the v1.1.1
round). Fixed locally (now says "2.0.0") but that edit is *not*
committed/pushed and never will be under the current `.gitignore` --
flagged to the user rather than unilaterally changing `.gitignore`
(their call whether that's intentional or an oversight).

**Build hygiene lesson, expensive to learn**: killing a `build_cuda.bat`
mid-compile (via `TaskStop` on the background task) does NOT clean up
`bin\` -- whatever CMake had already written there (in this case ~900MB
of CUDA runtime DLLs, staged early via an `install(FILES ...)`-style
step, well before final linking) sits there and silently contaminates
the *next* build, regardless of variant. Concretely: a killed CUDA build
left `cudart64_12.dll`/`cublas64_12.dll`/etc. in top-level `bin\`; the
next `build_plain.bat` run's move-to-`bin\plain\` step swept them in
too, producing a 664MB "plain" installer (should be ~32MB) that bundled
CUDA libraries a CPU-only build has no business shipping, and separately
made `test/gpu_solver_test.py` wrongly detect the plain build as CUDA-
capable (see [[gpu_speedup_investigation]] for the test-heuristic
detail). Fix used: `git clean -ndx bin/` (dry run first) then `-fdx` --
safe because the 6 static data files (`condlib.dat`, `heatlib.dat`,
`init.lua`, `license.txt`, `matlib.dat`, `statlib.dat`) are git-tracked
and `git clean` never touches tracked files, so it's the right tool for
"wipe build output, keep source assets" in this repo specifically.
**Rule going forward**: never trust `bin\` after killing a build
mid-flight -- `git clean -fdx bin/` before the next build, every time.

**GPU regression test results this round** (see
[[gpu_speedup_investigation]] for full detail and the older, conflicting
DC-speedup numbers): full plain-build suite clean (15 passed, 2 correctly
skipped, run in isolation -- no concurrent build). CUDA build's own
correctness checks pass perfectly on both DC and AC problems; AC/harmonic
GPU solve genuinely 3.16x faster, but DC/real-valued GPU solve measured
3x *slower* than CPU this round -- left as an honest failing assertion,
not investigated further (out of scope), not worked around.

## Round 11: real installer bug behind "the shortcut doesn't work" -- missing Qt6PrintSupport.dll (2026-07-20)

Committed `47b3414` on `new_features` (pushed), plus two CI-only commits
(`65d9a67`, `4bc1b38`) fixing an unrelated Qt6-install-in-CI mirror/module
issue found along the way (see [[build_and_com_registration_gotchas]] for
that one). User reported the installed app's Start Menu shortcut "doesn't
work" after installing the real `bin\cuda\FEMMX_v2.0.0_installer.exe` --
this turned out to be a second, genuine bug, unrelated to Round 10's
message-map fix: `script.nsi` bundled `femmqt.exe`'s Qt6 DLLs via
individually-named `File` lines, and `Qt6PrintSupport.dll` (added when
femmqt gained Print/Print Preview support) was never added to that list.
Every installed copy silently shipped without it. Symptom was NOT a loud
"DLL missing" dialog -- `femmqt.exe` would start, load 41 DLLs, then hang
forever (0% CPU, zero windows, `qwindows.dll` never loaded) -- see
[[build_and_com_registration_gotchas]] for the full diagnostic play-by-
play (module-count polling, CPU-delta deadlock check, `Compare-Object`
directory diffing, `dumpbin /dependents`) and the fix (switched to a
`Qt6*.dll` wildcard). Verified end-to-end this round: clean uninstall,
fresh silent install from the rebuilt installer, launched via the real
`FEMMX.lnk` Start Menu shortcut, screenshotted working.
**Also worth remembering**: ~13 failed test launches from before the fix
each left an undismissed native "System Error" dialog on screen (owned by
`csrss.exe`, not the failed app's own PID) -- a *later*, genuinely-working
launch's screenshot (a screen-region capture, not a true per-window
capture) visually picked up one of those stale overlapping dialogs,
making the fix look broken until the stale dialogs were found via
`EnumWindows` and closed. Don't trust a "still broken" screenshot without
checking what's actually still on screen from earlier test runs.
