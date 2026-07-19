---
name: femmqt-qt-gui
description: "New Qt6-based GUI (femmqt/) built alongside the classic MFC GUI, magnetics-only Phase 1, shipped and made the default -- what it covers, what it doesn't, and where the design decisions live"
metadata:
  type: project
  originSessionId: 846a52dc-e5cc-4b0f-9a4f-7b5debeae297
  modified: 2026-07-19T08:18:20.250Z
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
