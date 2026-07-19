---
name: femmqt-qt-gui
description: "New Qt6-based GUI (femmqt/) built alongside the classic MFC GUI: magnetics-only Phase 1, full property editing, and a menu-by-menu parity pass (zoom/pan/grid/undo/move/copy/scale/mirror, mesh split, post-processor analysis tools) -- what it covers, what it doesn't, and where the design decisions live"
metadata:
  type: project
  originSessionId: 846a52dc-e5cc-4b0f-9a4f-7b5debeae297
  modified: 2026-07-19T17:02:52.457Z
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
