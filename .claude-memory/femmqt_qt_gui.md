---
name: femmqt-qt-gui
description: "New Qt6-based GUI (femmqt/) built alongside the classic MFC GUI: magnetics-only Phase 1, full property editing, and a menu-by-menu parity pass (zoom/pan/grid/undo/move/copy/scale/mirror, mesh split, post-processor analysis tools) -- what it covers, what it doesn't, and where the design decisions live"
metadata:
  type: project
  originSessionId: 846a52dc-e5cc-4b0f-9a4f-7b5debeae297
  modified: 2026-07-22T16:53:41.291Z
---

**Current state (2026-07-21, supersedes Round 10 below): the CLASSIC GUI
is the default again, not Qt.** Round 10 made `FEMMX.lnk` launch
`femmqt.exe` exclusively; a later session reverted this per direct user
instruction ("QT has too many bugs at the moment, use the old GUI as the
default option") -- `script.nsi`'s `FEMMX.lnk` now launches `femmx.exe`
(classic), with a separate `FEMMX (Qt).lnk` for Qt. Also see
[[qt_gui_scope_deferred]]: as of the same session, new non-magnetics
physics-type work (heat/electrostatics/current-flow) is going into the
classic GUI + solvers only, not femmqt -- femmqt stays magnetics-only,
not being actively extended right now.

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

## Round 12: geometry editor zoom/pan stutter -- FullViewportUpdate, fixed by porting SolutionGraphicsView's own prior fix (2026-07-21)

User: "the problem i find is that when i zoom i[n] or move the geometry
is not smooth." Found via direct code reading (no Agent/subagent, per
the standing constraint from Round 5): `GeometryView`'s constructor set
`setViewportUpdateMode(QGraphicsView::FullViewportUpdate)` to fix a
stale-tooltip-trail bug (the floating cursor coordinate readout is a
plain `QLabel` child of the viewport, not a scene item, so
`MinimalViewportUpdate`'s scene-change-driven dirty tracking doesn't
know to repaint its old position when it moves). But `FullViewportUpdate`
forces a full repaint on **every** viewport update, not just tooltip
moves -- including every wheel-zoom (`scale()` in `wheelEvent`) and
every toolbar/scrollbar pan (`onPanLeft/Right/Up/Down` in
`MainWindow.cpp`, plain `scrollBar->setValue()` calls, which normally
get a cheap blit-and-fill-the-new-strip optimization that
`FullViewportUpdate` disables) -- exactly the reported stutter.

**The fix already existed in the codebase, just not ported.**
`SolutionGraphicsView::mouseMoveEvent` (`SolutionView.cpp`) had already
solved the *identical* tooltip problem the right way:
`scene()->invalidate(mapToScene(oldGeometry).boundingRect())` on just
the tooltip's vacated rect -- `scene()->invalidate()` is the mechanism
`MinimalViewportUpdate` actually respects for "redraw this region even
though nothing scene-side changed." Its own comment even names a PRIOR
"everything is slow on large geometries" complaint that motivated that
fix -- meaning this exact class of regression had already been found
and fixed once, in the solution viewer, and just never got applied to
the geometry editor. Ported it: removed `GeometryView`'s
`FullViewportUpdate` call entirely, added the same
`scene()->invalidate()` call to its `mouseMoveEvent`. Zoom and pan now
go through Qt's efficient default update path; only genuine mouse-hover
moves pay the (correctly-scoped) invalidate cost, same frequency as
before.

**Lesson**: when two sibling classes solve the same UI problem (here:
`GeometryView` and `SolutionGraphicsView`, both `QGraphicsView`
subclasses with an identical floating-tooltip pattern), check whether
one already has a better fix before re-deriving one -- grep for the
similar class name/pattern first, don't assume symmetry has to be
re-verified from scratch every time.

**Verified live**: built via `build_qt` (`cmake --build build_qt --target
femmqt`), then launched the real exe against a loaded `.fem`, swept the
cursor across the canvas with synthetic `SetCursorPos` calls, and
screenshotted -- confirmed exactly one tooltip label at the final
position, no stale trail left behind anywhere along the path. Confirms
`FEMMQT_HAVE_OPENGL` is active in this build too (`QOpenGLWidget`
viewport, per the constructor's `#ifdef`), so the fix is layered on top
of already-GPU-composited rendering, not fighting it.

**Separately investigated but not (yet) fixed**: `MeshSolutionItem`'s
constructor (`SolutionView.cpp:104-166`, runs synchronously on the UI
thread every time a solved file opens) does 6 full passes over every
mesh element -- one per `DensityQuantity` enum value -- eagerly
precomputing node-averaged values and min/max ranges for all 6 plot
quantities, even though a session typically only ever looks at 1-2.
Not captured by the file's own `QElapsedTimer` load-time profiling
(that timer stops *before* this constructor runs) so it's an invisible
cost in whatever load-time numbers already exist. Recommended fix:
lazy per-quantity computation on first actual use instead of eager-all-6
at construction. Not implemented -- flagged to the user, awaiting a
decision on whether it's worth doing (tradeoff: switching density
quantities mid-session would get a one-time hitch instead of being
instant, in exchange for a faster initial big-file open).

## Round 12b: floating tooltip removed entirely; mesh-point-dots-grow-when-zoomed bug fixed (2026-07-21, same day as Round 12)

Follow-up to Round 12 (zoom/pan stutter fix). User: "if the moving text
box slows down things, remove and make it stationary somewhere." Rather
than keep tuning `GeometryView`'s floating cursor-following tooltip
(the thing that needed the `scene()->invalidate()` workaround at all),
removed it entirely -- `GeometryView::mouseMoveEvent`/`leaveEvent` are
now just the base `QGraphicsView` behavior (no override needed). The
stationary status-bar coordinate readout (`MainWindow::m_positionLabel`,
wired independently via `GeometryScene::mousePositionChanged`, added
back in Round 7) shows the same x/y info with zero per-move viewport
cost -- confirmed nothing was lost by checking it live after the
removal. **Left `SolutionGraphicsView`'s identical floating tooltip
alone** -- user's ask was scoped to "the geometry" (the editor), and
that one already uses the cheap `scene()->invalidate()` approach with
no reported complaint against it.

User then asked: "when the geometry is zoomed, I want the edges and
points to not appear thicker -- is this some kind of vector graphics?"
plus "same in solution viewer, I want an option to show the edges and
points." Investigated both:

- **Answer to the conceptual question**: it's not really about "vector
  vs. raster" per se (an SVG zoomed via image scaling would ALSO get
  thicker lines) -- it's specifically whether the pen width is defined
  in *device pixels* (a "cosmetic" pen, `QPen::setCosmetic(true)`) vs.
  *scene/world units* (the default, which DOES scale with zoom). The
  classic GDI-based GUI gets the same constant-width effect via
  `PS_COSMETIC` pens.
- **Geometry editor**: already correct in current source --
  `GeometryScene.cpp` sets `pen.setCosmetic(true)` on every segment/arc/
  node pen, and `NodeItem`/block-label markers already use
  `ItemIgnoresTransformations` for constant on-screen size. Tested
  directly (built via `build_qt`, zoomed ~264x on a real loaded model,
  screenshotted before/after) and could NOT reproduce any thickening --
  edges and node dots stayed visually the same size. **If the user still
  sees this, they're very likely testing an older installed/pre-built
  exe** (the Start Menu shortcut, or a `bin\plain\`/`bin\cuda\` copy from
  earlier in this session), not current source -- worth asking which
  exe before assuming a live bug exists.
- **Solution Viewer -- real bug found and fixed**: "Show Mesh" and
  "Show Points" already existed as checkable View-menu items
  (`SolutionView.cpp`'s `viewMenu->addAction("Show &Mesh"/"Show
  &Points")`, wired since Round 4) -- confirmed live via a real menu
  click + screenshot that "Show Mesh" already renders correctly. But
  `MeshSolutionItem::paintMeshOverlay`'s "Show Points" dots WERE
  genuinely growing with zoom: `drawEllipse(pos, r, r)` used
  `r = model_diagonal * 0.0015`, a fixed SCENE-space value, unlike the
  mesh edges just above (a cosmetic `QPen`, correctly zoom-invariant).
  `QPen::setCosmetic` only affects *stroked* lines, not a filled
  shape's own geometry -- a filled circle needs the equivalent done by
  hand: divide a fixed on-screen pixel radius by
  `painter->worldTransform().m11()` (the app only ever does uniform
  `scale()` zooming, never skew, so `m11()` alone is a safe proxy for
  the current zoom factor without needing a full singular-value
  decomposition). Fixed, verified live (built, enabled Show Points,
  screenshotted before/after a ~13x zoom -- dots stayed small and
  consistent instead of growing into blobs).

**UI-automation gotcha hit again this round**: keyboard menu mnemonics
(`Alt+V` then a letter) misfired at least once -- landed on "Contour
Plot" instead of "Show Mesh"/"Show Points", turning the whole density
plot into a solid-white contour render that looked like a rendering bug
at first glance. Switched to direct mouse clicks on the menu (open View,
screenshot to find the exact row, click it) instead, which worked
reliably. **Also**: `SolutionWindow`'s actual canvas center is NOT the
client-rect center -- the docked Output Window panel eats a large
fraction of the bottom of the window, so a wheel-zoom aimed at the
naive client-rect midpoint can land on the Output Window instead of the
canvas and silently do nothing (looked like "zoom doesn't work" until
traced to aiming at the wrong widget). Use a screenshot-verified
canvas-area point instead of assuming client-rect center is inside the
graphics view for this specific window.

## Round 12c: "white screen" in Contour Plot root-caused (not fixed); original problem geometry was entirely missing from the Solution Viewer (fixed) (2026-07-21, same day)

User: "i see a white screen there, is it supposed to look like this?"
(Contour Plot mode, the default -- see Round 12b -- on
`gpu_solver_cpu.ans`, a wire+ABC magnetics model).

**Root-caused, not yet fixed**: confirmed `paintContour`'s algorithm
itself is correct by opening a clean reference file
(`straight_wire_field.ans`) -- renders textbook concentric circles.
Directly measured `gpu_solver_cpu.ans`'s node values (two ways: parsing
`[Solution]` in the raw `.ans` text directly, AND a slow ~38K-node
`mo_getpointvalues` COM loop as a cross-check) -- the vector potential
A itself has NO extreme outlier values (smooth percentile distribution,
p1-p99 span is 72% of the full span). But switching the SAME file to
Density Plot revealed the real story: `|B|` is anomalously HIGH in a
ring right at the outer domain boundary (>3.66e-3 T there vs. ~2-6e-4 T
everywhere else) -- almost certainly from `mi_makeABC`'s multi-layer
Kelvin-transform boundary construction. Since Contour Plot spaces its
20 levels evenly across A's *value* (not radius), and that thin outer
boundary region evidently accounts for a large fraction of A's total
dynamic range despite being geometrically thin, nearly all 20 contour
lines end up compressed into/near that boundary -- exactly what the
NEW geometry-overlay feature (below) then visually confirmed: you can
literally see the multiple closely-packed ABC layers there once
segment/arc geometry is drawn. **Offered but not implemented**: making
Contour Plot's level computation more robust (e.g. percentile-based
instead of true min/max) -- user hasn't asked for this yet.

**Real, separate feature gap found and fixed**: user's follow-up, "in
the solution viewer the edges and nodes of the geometry do not show
up." Checked `femm/FemmviewView.cpp` directly -- the classic GUI
ALWAYS overlays the original problem geometry (nodelist/linelist/
arclist -- the actual nodes/segments/arcs drawn in the pre-processor,
e.g. "20 nodes, 20 arcs") on top of whichever plot is active in the
post-processor, completely distinct from the solved FE MESH (millions
of elements). femmqt had genuinely nothing for this -- "Show Mesh"/
"Show Points" only ever meant the solved mesh's own triangulation/
nodes. Added `MeshSolutionItem::setProblemGeometry()`/
`paintProblemGeometry()` (`SolutionView.cpp`/`.h`): segments/arcs as
cosmetic-pen lines (`AppTheme::segmentColor()`/`arcColor()`, matching
the geometry editor's own colors -- arcs via an independent copy of
`GeometryScene.cpp`'s `arcGeometry()` math, this codebase's established
precedent over a shared header), nodes as small fixed-screen-size
squares (same divide-by-`worldTransform().m11()` technique as the
mesh-point-radius fix two rounds ago). Always drawn when available, no
new toggle -- matches classic's own unconditional behavior for
segments/arcs.

`SolutionWindow` didn't have the original `FemmProblem` at all before
this (only the `MeshSolution`) -- `openAnsFile()`'s two load paths
needed different plumbing: the slow (.ans direct) path already parses
`FemmProblem` as a side effect of extracting `problemType`/
`lengthUnits`/`frequency`, so it's now just kept (`m_problemGeometry =
problem;`) instead of discarded; the fast (.ansx cache) path has
nothing to reuse (the cache only ever stores the mesh, by design), so
it does one extra best-effort `FemmFileIO::readFem()` call against the
`.ans` file -- already proven safe/fast against `.ans` files elsewhere
in this exact class (`onProblemInfoTriggered`/`onCircuitPropsTriggered`/
`onBhCurvesTriggered` all do the same thing already).

Verified live against both files mentioned above -- concentric boundary
arcs and node markers now clearly visible over the density plot in
both, and neither grows when zoomed (confirmed by construction, reusing
the just-established fixed-screen-size technique, not just assumed).

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

## Round 13: spurious vertical line when arcs form a circle -- Qt's arcTo angle convention needed a sign flip; undo extended to a 20-step stack (2026-07-21/22)

User reported a spurious straight line appearing when arcs are drawn to
form a circle, in both the geometry editor and Solution Viewer, most
visibly on ABC-boundary shell circles (e.g. `test/results/straight_wire_field/straight_wire_field.fem`'s outermost, boundary-marked shell pair).
Root-caused via a minimal from-scratch repro (a 2-node, 1-or-2-arc `.fem`,
isolating every variable the real file had -- mesh overlay, block labels,
9 nested shells, etc.) rather than continuing to stare at the 20-arc
file: a single 180-degree, exactly-vertical arc reproduced it standalone,
proving the CURVE itself rendered correctly (both semicircle bulges
present, correctly shaped) but grew an EXTRA straight bridging line from
the `moveTo()` point to a second point.

Cause: `arcGeometry()`'s `startAngleDeg = atan2(y0 - cy, x0 - cx)` used
plain math atan2, but `QPainterPath::arcTo`'s own point-at-angle formula
effectively negates the y-term (Qt's documented convention: 0 deg = 3
o'clock, 90 deg = 12 o'clock, even though scene y increases downward --
confirmed by deriving the y0=2*cy-y0 mirror relationship and checking it
against Qt's own worked example). Whenever Qt's own arc-start point
(computed from our `startAngleDeg` via ITS formula) didn't match the
`moveTo()` point we'd already placed, Qt bridged the two with a straight
line before drawing the curve. For a 180-degree, purely-vertical arc,
the y-mirror of the start point lands EXACTLY on the opposite node --
maximally visible as a full diametric line -- which is why this went
unnoticed for non-vertical/non-180 arcs (the mismatch there is a shorter,
easy-to-miss bridging segment or a wrong-bulge-direction curve, not a
dramatic full-height line).

Fix: negate the y-term -- `atan2(-(y0 - cy), x0 - cx)` -- applied
identically to both independent copies of `arcGeometry()`
(`GeometryScene.cpp` and `SolutionView.cpp`, per this codebase's
established precedent of independent copies over a shared header, see
Round 9's note on the same function). Verified empirically, not just by
re-deriving the math again: (1) minimal 1-arc and 2-arc vertical repros
both went from "curve + spurious diameter line" to "clean curve, no
line"; (2) the real `straight_wire_field.fem` file, opened fresh (after
deleting its stale `.femx` cache) with NO click performed, rendered with
zero stray lines -- the earlier "still broken" screenshot in this same
investigation turned out to be a SEPARATE artifact: the repro script's
own diagnostic click (deliberately aimed at the old bug's line location)
was landing on the now-correctly-positioned arc's edge and selecting it,
and Qt's default dashed-selection-rectangle outline (one edge sitting
exactly on the shared node x-coordinate) was being mistaken for a
surviving version of the original bug; (3) an asymmetric, off-axis,
non-90-degree arc (endpoints (30,5) and (10,25), 75-degree sweep)
rendered identically in femmqt and the classic GUI (via COM automation
screenshot comparison) -- confirms the fix produces the geometrically
*correct* bulge direction, not merely "no visible line" for the
symmetric cases that happened to be tested.

Same session, follow-up request: extend `MainWindow`'s undo from a
single overwritten snapshot (`m_undoSnapshot`/`m_hasUndo`, matching
classic FEMM's own single-slot `CFemmeDoc::UpdateUndo`/`Undo`) to a
20-step stack. Mechanical change -- `QList<FemmProblem> m_undoStack`
capped at `kMaxUndoSteps = 20` (push in `snapshotForUndo()`, evicting the
oldest via `pop_front()` once over the cap; `onUndoTriggered()` pops via
`takeLast()`) -- covering the exact same 7 call sites as before (Create
Radius, Create Open Boundary, Import DXF, Move/Copy/Scale/Mirror
Selected, and Delete Selected via the `aboutToEdit` signal); still no
redo (wasn't requested, and classic FEMM doesn't have one either).
Verified two ways, both via UI automation with numeric ground truth
(reading saved `.fem` node coordinates back, not just eyeballing
screenshots): (1) 3 sequential single-node deletes followed by 4 Ctrl+Z
presses showed the node count step correctly back up 2->3->4->5 with the
status bar reporting "Undone (N more steps available)" at each step, and
the 4th (extra) undo correctly reported "Nothing to undo"; (2) a 25-node
file with 25 sequential `Move (+1,0)` operations (using a rubber-band
re-select before each -- `rebuild()` after a move does not preserve the
graphics-item selection state, so a stale selection would silently hit
"Nothing selected" on the 2nd+ iteration if not re-selected) followed by
21 Ctrl+Z presses: saving and reading the `.fem` back at each checkpoint
showed +25mm after the moves, +5mm after 20 undos (exactly 20 of 25
recovered, confirming the cap evicts the oldest 5), and still +5mm
unchanged after the 21st undo (confirming it correctly no-ops once the
stack is empty rather than under- or over-shooting).

## Round 14: batch (multi-select) property editing to match classic FEMM; a real matlib.dat parser bug found and fixed (2026-07-22)

User: "make sure the selection and editing of the edges and node
properties happens same way as in the old gui. Also make sure the
material library drop down list works fine."

**Selection/editing gap, found by reading femm/FemmeView.cpp +
FemmeDoc.cpp cold rather than assuming femmqt already matched**: classic
FEMM's actual convention is LEFT click = add geometry (mode-dependent),
RIGHT click = toggle-select nearest item of the CURRENT mode's type,
and **Space bar / Edit > Open Selected (ID_OPEN_SELECTED)** opens one
property dialog for the ENTIRE current selection at once --
`CFemmeDoc::OpNodeDlg`/`OpSegDlg`/`OpArcSegDlg`/`OpBlkDlg` each loop over
every `IsSelected==TRUE` entry and apply the dialog's chosen values to
all of them on OK, not just one. femmqt already had "Open Selected" as a
menu item (matching the name) but `GeometryScene::selectedEntity`
required exactly one selected item ("Select exactly one item first") --
the batch-apply behavior itself was simply missing, and there was no
Space-bar shortcut at all (classic binds it in `OnKeyDown`, not as a
`QKeySequence` on a menu action, so nothing in femmqt's own menu/
shortcut wiring would have surfaced this gap without reading classic's
mouse/keyboard code directly).

Fixed: `GeometryScene::selectedEntities(FemmItemKind&, QVector<int>&)`
replaces `selectedEntity` -- true only when the selection is non-empty
and every item is the same kind (mixed kinds can't happen in classic's
own model, so mixed femmqt selections just get a clear "select entities
of only one kind" message rather than an attempted guess). A new
`GeometryScene::openSelectedRequested()` signal fires on `Key_Space`
(`keyPressEvent`, alongside the existing Delete/Backspace handling),
connected to `MainWindow::onOpenSelectedTriggered` -- same signal/slot
pattern already used for `aboutToEdit`/`entityDoubleClicked` rather than
hardcoding dialog logic in the scene. All four PropDialogs
(`NodePropDialog`/`SegmentPropDialog`/`ArcPropDialog`/
`BlockLabelPropDialog`) changed from taking a single `FemmXxx&` to a
`QVector<FemmXxx*>`, replicating each classic `OpXxxDlg`'s own specific
mixed-value rule (verified by reading `femm/FemmeDoc.cpp` line by line
for each, not guessed): boundary/point-property mixed -> "<None>";
mesh size mixed -> average across the batch (segments: automesh wins if
ANY selected is on automesh); hidden mixed -> checked if ANY selected is
hidden; group mixed -> 0. `BlockLabelPropDialog` needed its own twist:
classic's mixed-material default ("<None>", index 0) isn't safely
reusable here because index 0 in femmqt's combo is "<Hole>" (a
deliberate earlier simplification, see Round 5) -- collapsing a mixed
batch to "<Hole>" on an unattended OK would be destructive, so a mixed
material/circuit instead shows a non-selectable "<Multiple>" placeholder
and `onAccept()` only touches that field if the user actually moved off
it, otherwise leaving each label's existing value untouched.
`MainWindow::openEntityProperties(kind, indices)` is the new shared
entry point both `onOpenSelectedTriggered` (whatever's selected) and
`onEntityDoubleClicked` (a single hit-tested item, wrapped in a
one-element `QVector`) funnel through, so double-click and Space bar/
Open Selected now share one code path instead of two.

**Verified live, all via UI automation with numeric/visual ground
truth, not just code review**: (1) Ctrl+click-selected 2 segments
(different indices, both initially unassigned), Space bar opened
"Segment Properties (2 segments)" showing Boundary "<None>" (correctly
uniform), set it to a real boundary, OK, saved, and read the `.fem`
back -- both selected segments' `boundaryMarker` changed, the two
unselected ones didn't. (2) Selected 1 node + 1 segment (mixed kinds),
Space bar correctly showed "Select entities of only one kind..." instead
of guessing. (3) Double-clicking a single segment still opens the
unchanged-looking "Segment Properties" (no count suffix) dialog --
confirms the shared-path refactor didn't regress the existing single-
item flow.

**Materials Library dropdown bug, found empirically not by inspection**:
opening Problem > Materials Library and expanding into any category
folder (e.g. Soft Magnetic Materials > Silicon Iron) showed a
row of completely blank space where the actual material names should
be -- but the rows were real: clicking a blank one and hitting "Add to
Problem" correctly added the right material by name every time,
proving the underlying data was fine and only the tree's own display
text was missing. Spent real effort ruling out the wrong causes first
(confirmed live): not a dark-theme color/contrast issue (a diagnostic
pass explicitly forcing every tree item to bright red still showed nothing
in those rows), not a stale-layout/repaint issue (`expandAll()` +
`collapseAll()` at construction time, and manual scroll/resize, changed
nothing). Root cause, found by reading `MaterialLibraryIO.cpp`'s
`readChildren()`: for a folder child it sets `child.name` from
`<FolderName>` (so folders display fine), but for a material/block
child it only ever set `child.material.name` (from `<BlockName>`, via
`readBlock()`) -- the outer `MaterialLibraryNode::name` field that
`MaterialLibraryDialog::populateTree()` actually displays was simply
never assigned for non-folder nodes, at any depth. Fixed with one line
(`child.name = child.material.name;` right after a successful
`readBlock()`). This means the feature was never fully visually usable
before this fix (in the fast-iteration `build_qt/femmqt/Release/`
target, which also lacked `bin/plain/matlib.dat` entirely until copied
over for this round's testing -- same "install step's extra files don't
follow the fast-iteration build target" pattern as `triangle.exe`,
already documented in [[build_and_com_registration_gotchas]]) -- verified
live after the fix: "Low Carbon Steel" now shows "1006 Steel"/"1010
Steel"/"1018 Steel"/"1020 Steel"/"1117 Steel", all real grade names, in
normal readable text.

**Density Plot: invisible geometry overlay bug, root cause + fix
(2026-07-22, commit `134a40c` on `new_features`, pushed).** User report:
"show the geometry's edges together in the solution viewer, only the
block labels are currently visible" -- reproduced on the real 1.5M-node
transformer model (`mag_trafo_center_detailed_AC_a.ans`) in Density Plot
mode: the segment/arc overlay (`paintProblemGeometry`, drawn last, on
top) was rendering but effectively invisible; only the small node-marker
squares (which the user called "block labels") showed. Root cause:
`SolutionView.cpp`'s `bandColor()` was a fresh HSV "blue(240deg) ->
red(0deg)" ramp -- band 0 (lowest value, dominant in any real model's
low-magnitude background/air regions) was pure blue, colliding with
`AppTheme::segmentColor()`/`arcColor()`'s own light-blue/light-green
dark-theme colors. Confirmed via `femm/StdAfx.h`: classic FEMM's actual
20-entry density palette (`dColor00..19`) runs magenta -> red -> orange
-> yellow -> green -> cyan and deliberately never touches blue -- why
classic's own near-identical dark-theme line color (`RGB(90,160,255)`,
`femm/FemmviewView.cpp`) never had this problem. Fixed by porting that
literal table (plus `dGrey00..19` for a new Greyscale toggle) in place
of the HSV ramp -- verified via a before/after crop on the real model:
before, zero visible boundary line against the fill; after, a clear blue
line against the new magenta/orange/cyan palette. Same commit also added
a "Density Plot Options..." dialog (Greyscale + a per-`DensityQuantity`
opt-in custom Min/Max range, `MeshSolutionItem::setCustomRange`/
`clearCustomRange` -- the existing zoom-adaptive auto-range stays
default) and replaced the "average the 3 corners into one flat color"
Smoothing approximation with classic's real marching-triangle band-
slicing algorithm (`sliceTriangleIntoBands`/`appendEdgeCrossings` in
`SolutionView.cpp`) -- confirmed classic does NOT use GDI's
`GradientFill` (zero matches grepping all of `femm/` for
`GradientFill`/`Gouraud`/`TRIVERTEX`), correcting a stale comment that
had claimed otherwise.

**UI-automation gotcha, cost most of a session to pin down: a
`QMenu`'s native popup-tracking loop intermittently rejects synthetic
input from a separate process, with no useful error.** Pattern: `click`
to open a top-level menu (e.g. the View menu) always worked; a second
`SetCursorPos` to click an item *inside* that open popup would
frequently fail with `pywintypes.error: (0, 'SetCursorPos', 'No error
message is available')` -- sometimes on the very first attempt,
sometimes after several successful runs, with no code change in
between (confirmed: the target process stayed alive and its window
handle stayed valid throughout every failure, via `proc.poll()`/
`win32gui.IsWindow` checks -- ruling out a crash). `keybd_event`-based
Down+Enter navigation (instead of a second click) failed the same way,
proving it isn't mouse-specific. `win32process.AttachThreadInput`
(attaching this script's input queue to the target window's thread for
the whole open-menu-then-click sequence, not just an initial
foreground-steal) fixed it MOST of the time but not always -- the one
combination that was reliable across many repeated attempts was
AttachThreadInput *plus* a longer settle delay (>=1.5s, not the ~0.3-0.5s
that had been fine for ordinary non-menu clicks all session) between the
click that opens the menu and the click on an item inside it. Also
observed once: a `QDialog` opened from inside one of these flaky
sequences itself showed "(Not Responding)" in its own title bar with no
real work happening -- almost certainly leftover confused input-queue
state from a prior failed attempt in the same script, not a real hang
(a completely fresh process + single continuous script recovered
immediately). Takeaway for next time: for ANY click targeting something
*inside* an already-open `QMenu`/popup in this app, default to
`AttachThreadInput` wrapping the whole interaction plus a >=1.5s pause
after opening the menu before clicking the item, and if a window or
dialog ever shows "(Not Responding)" mid-automation-script with no
plausible heavy computation to justify it, kill that process and start
a fresh one rather than continuing to poke at the same stuck instance.

**Follow-up same day: greyscale contrast was genuinely too low, user
caught it live (commit `9311573`).** After the palette-swap fix above,
user directly tested and reported "I see no gradient" in Greyscale mode
specifically -- initially suspected a code bug, but a from-scratch
re-verification (sampling the real solved `|B|` field at 3600 points via
the classic GUI's own `mo_getb`, not trusting screenshots) showed the
20-band assignment itself was correct; the real problem was
`kGreyMap`'s range, ported verbatim from `femm/StdAfx.h`'s
`dGrey00..19` (55->245, 10-unit steps between bands) -- far below what's
perceptible, especially next to the color palette's large hue jumps.
Confirmed by directly rendering the real transformer model in Greyscale
and looking at it: technically-correct-but-imperceptible banding, exactly
matching the report. Fixed by widening to the full 0->255 range (~90%
more contrast per band) -- a deliberate improvement beyond classic's own
value, not a faithful-port choice (unlike the color table, which stayed
verbatim). **Lesson: when a user directly disputes something you already
called "verified," re-derive from first principles (real data, direct
code re-reading) rather than defending the earlier screenshot-based
verification -- this session's automation was flaky enough (see above)
that an earlier "successful" verification screenshot is not strong
evidence on its own.**

**Also confirmed this round: verifying a UI default via live automation
can be sidestepped entirely by temporarily changing the C++ default
value, rebuilding, and screenshotting a fresh launch -- no menu
interaction needed at all.** Used this after the interactive
Greyscale-checkbox toggle failed repeatedly across mouse clicks, Tab/
Space keyboard nav (initial dialog focus turned out to land on the
"Automatic" radio button, not the first-added checkbox -- Qt's initial-
focus choice does not necessarily match widget creation order), and
direct dialog-hwnd targeting -- all while the dialog and/or main window
intermittently either silently no-op'd or showed "(Not Responding)".
Temporarily flipped `MeshSolutionItem::m_mode`'s default to `Density` and
`m_grayscale`'s default to `true` in `SolutionView.h`, rebuilt, launched
fresh (zero interaction needed beyond waiting for load), screenshotted,
then reverted both defaults and rebuilt again before committing --
verified the revert was exact via `git diff` showing zero changes to
that file. For a huge model (the 3M-element transformer file), this
also needs the load-wait lesson from earlier in this doc: a short/naive
CPU-stability check gives false positives mid-construction (seen again
this round, a "stable" reading after only 39s when the real settle point
was 150s+) -- prefer `Get-Process`'s own `.Responding` flag polled every
few seconds over guessing from `cpu_times()` deltas. Even `.Responding`
itself gave one false-positive "responsive after 0.3s" right after a
fresh launch on a later round -- treat a `.Responding=True` reading in
the first ~10-20s post-launch with suspicion too, not just
`IsHungAppWindow`; when in doubt, add a real minimum-elapsed floor before
trusting either signal (`$sw.Elapsed.TotalSeconds -gt $minWait`).

**Same day, third bug: Vector Plot "looks weird" on the transformer model
(commit `772eb39`).** `paintVector()` sampled arrows by striding through
`m_solution->elements` at a fixed ARRAY-INDEX step
(`elements.size()/3000`) and sized every arrow off the WHOLE model's
diagonal -- both assumptions silently depend on the mesh being roughly
spatially uniform. On the real transformer file (fine mesh around the
coil conductors, coarse mesh in the surrounding air -- a completely
ordinary real-world mesh, not a pathological case), index order wasn't
spatial order: the stride landed nearly all its samples inside the
densely-meshed coil regions (rendering as solid white blobs from
heavily-overlapping oversized arrows) and almost none in the coarse
exterior (~10 stray specks visible). Fixed by sampling on a fixed 36x36
SCREEN-SPACE grid over the exposed rect instead (one representative
element per cell via the existing `elementsOverlapping` spatial index),
with arrow length scaled to the cell size rather than the whole model.
**General lesson for this mesh-heavy codebase: never assume element
ARRAY INDEX correlates with spatial position, and never size a
screen-drawn feature off the WHOLE model's extent when local mesh
density can vary by orders of magnitude** -- both of the vector-plot
bugs above have the same shape as the very first "invisible edges" bug
this session (a rendering choice that implicitly assumed the mesh/data
was more uniform than any real FEM model actually is). When something
"looks weird" on a real model but the test/toy files look fine, check
for exactly this class of uniformity assumption first.

**Verification method used for both bugs above, faster and more reliable
than fighting interactive menu automation on this huge file**:
temporarily change the relevant default in `SolutionView.h` (e.g.
`MeshSolutionItem::m_mode`'s or `m_grayscale`'s in-class default value),
rebuild, launch fresh with ZERO interaction needed beyond waiting for
load, screenshot, then revert the default and rebuild again -- confirm
the revert is exact via `git diff` showing no changes to that file
before committing the REAL fix. Only commit the file(s) containing the
actual fix; never let a temporary debug default leak into a commit.
