# Unit Tests

Pytest-based regression tests that build and solve real FEMM models
entirely from Python, using the `pyfemm` COM interface to a locally built
`femmx.exe`. These run automatically in CI (`.github/workflows/ccpp.yml`,
after `femmx.exe` is built) and can be run locally the same way.

Every test module writes its generated files (models, solutions, reports)
under `results/<script_name>/`. The `.txt` reports are tracked in git as
evidence of the last run; model/binary artifacts (`.fem`, `.ans`, `.res`,
`.dxf`, `.bmp`, mesh intermediates) are regenerated on each run and
gitignored.

### Prerequisites

- A built `femmx.exe` (see the repository root `README.md` / `build.ps1`),
  registered as a COM automation server (`femm.ActiveFEMM`) -- run
  `scripts/register_femm_com.ps1` after building if it isn't already
  self-registered.
- Python packages: `pip install -r requirements.txt`

### Running

```
pytest test/ -v
```

Each module can also still be run directly as a script, e.g.
`python test/straight_wire_field_test.py`. If `femmx.exe`/COM
automation isn't available, the whole suite is skipped (not failed) with a
clear reason -- see `conftest.py`.

## straight_wire_field_test.py

Builds a 2D planar magnetostatics problem: a single current-carrying wire
(10 A) surrounded by an open-boundary air domain. Solves it and asserts the
computed flux density at a probe point matches the closed-form solution for
an infinite straight wire (Ampere's law) within 2%.

Output: `results/straight_wire_field/straight_wire_field.{fem,ans}`.

## corrupt_input_test.py

Nothing in the suite fed bad input to anything. `.fem`, `.ans`, `.femx`,
`.ansx` and `.dxf` are all parsed by hand-rolled readers in both GUIs and in
four solver binaries, and a truncated file is the normal outcome of an
interrupted solve or a half-copied file -- a realistic input, not a fuzzing
exotic (issue #10). Variants are generated systematically from real valid
files: empty, truncated at 10/50/90%, garbage appended, garbage only, and
for the binary caches a zeroed header and an inflated count field.

| Target | Result |
| --- | --- |
| `fkn`, `belasolv`, `hsolv`, `csolv` | every variant exits 2, promptly, no crash |
| `femmqt --convert-ansx` | rejects every truncation with exit 1 |
| `femmqt --import-dxf` | rejects cleanly, no crash |
| Corrupt `.femx` / `.ansx` cache | all 7 variants fall back to the text source and load |
| COM `opendocument` | session stays responsive through every variant |

**THE SOLVERS HAVE A NON-INTERACTIVE MODE AND IT IS NOT OPTIONAL FOR
AUTOMATION.** `fkn/StdAfx.cpp`:

```cpp
int MsgBox(CString s) {
  if (__argc < 3) return AfxMessageBox(s);   // modal, blocks forever
  else            return IDOK;               // suppressed
}
```

So `fkn.exe model` pops a modal dialog and hangs with no user to dismiss it,
while `fkn.exe model anything` exits with a status code. The GUI passes the
literal `bLinehook` as that second argument (`femm/FemmeView.cpp`).
Measured: one-argument invocations on truncated, empty and garbage input all
sat there until killed at 15s; two-argument invocations of the same files
all exited 2 promptly. **Any CI or batch script driving a solver directly
must pass a second argument.** A test pins this, because the cost of not
knowing it is a build that hangs rather than fails.

**This found a real defect**, now fixed in `femmqt/AnsFileIO.cpp`: a
truncated `.ans` was read as a SUCCESS. `readAns` returned `true`
unconditionally, so when truncation cut the node list short, the following
"element count" line was really EOF, `strtol("")` gave 0, and the reader
produced a mesh with nodes and no elements. Measured before the fix: a
10%-truncated file converted to a `.ansx` claiming **9469 nodes and 0
elements**, exit code **0**. It now detects EOF mid-list and rejects a
node-bearing mesh with no elements, naming the file and the counts.

One behaviour left as-is and documented rather than changed: FEMM accepts a
garbage `.fem` over COM without complaint (producing an empty model rather
than an error). The session stays responsive, which is what is asserted;
tightening the `.fem` text reader to reject files it currently tolerates is a
larger change than this ticket.

Output: `results/corrupt_input/`.
## material_library_test.py

`MaterialLibraryIO.cpp` / `BHCurve.h` parse the library every nonlinear
magnetics model depends on, and a regression there silently changes solved
results rather than failing loudly (issue #9). This runs against the
libraries the installer actually ships (`bin/*.dat`), parses them with an
INDEPENDENT Python reader -- a third opinion rather than a reuse of the code
under test -- and cross-checks a sample against what FEMM itself loads.

| Check | Result |
| --- | --- |
| Shipped libraries parse | matlib 246, heatlib 136, statlib 26 entries |
| `<BHPoints> = N` matches the rows that follow | all 87 nonlinear materials |
| BH curves monotonic and starting at the origin | all 87 |
| `mi_getmaterial` reproduces the library | 5 materials, 40 values + every BH point identical |
| Scripted BH curve round-trips | 7 points in, 7 out |
| `mi_clearbhpoints` empties it | 7 -> 0 |
| Lamination / stranded-wire properties survive save/load | LamType, LamFill, d_lam, NStrands, WireD |
| A deliberately broken library is detected | proves the shipped-library checks are not vacuous |

**This found a real data defect in the shipped libraries** (filed as #34, and
pinned here rather than silently "fixed"): two entries share a name with a
different material, and `mi_getmaterial` looks up by name alone.

- `matlib.dat` has two `Supermalloy` -- `mu_r` 529095 with 12 BH points in
  Nickel Alloys, and `mu_r` 1 with 34 points in Metals Handbook DC
  Magnetization Curves.
- `heatlib.dat` has two `Ammonia` -- `k` = 0.546 in Saturated Liquids and
  `k` = 0.0153 in Gases at 1 atm, a factor of 36 apart.

The GUI browser disambiguates by folder; scripting cannot. Verified against a
real `femmx.exe`: the first in file order wins, so the other is unreachable
from a script entirely. A test pins which one that is, so a reordering of the
library is caught, and any NEW duplicate fails the build. Renaming entries
upstream ships and users reference by name is a data decision with an owner,
not something a test should do quietly.

Output: `results/material_library/`.
## dxf_roundtrip_test.py

DXF is how real geometry gets into FEMM from CAD, and there are two
independent implementations in this tree: classic FEMM's `MOVECOPY.CPP`
`ReadDXF`/`WriteDXF`, and `femmqt/DxfIO.cpp`, a direct port of it. Neither
had a single test -- the Lua sweep writes a `.dxf` but never reads one back
or looks at what it wrote (issue #8).

Fixtures live in `test/fixtures/dxf/`, hand-written and committed, one
entity type per file so a failure names the entity:

| Check | Result |
| --- | --- |
| Export -> import round-trip (classic) | 6 nodes / 4 segments / 1 arc preserved |
| LINE, ARC, CIRCLE, LWPOLYLINE, POLYLINE import | all five |
| Arc geometry | endpoints on r=10, included angle 90.000 deg |
| CIRCLE becomes closed arcs | 2 arcs totalling 360.000 deg |
| Layers become groups | 3 declared layers -> groups [0, 1, 2] |
| Both implementations agree | identical edges on 5 fixtures |
| Merge tolerance | femmqt at its suggested tolerance matches classic exactly |
| Malformed input | both reject cleanly, no crash |

**Cross-implementation comparison needed a way to reach femmqt's parser**
without driving the GUI -- classic's is scriptable via `mi_readdxf`,
femmqt's was not. This ships with a `femmqt.exe --import-dxf <in.dxf>
<out.fem> [tolerance]` CLI mode mirroring the existing `--convert-ansx`.

Three things worth knowing, all learned the hard way while writing this:

- **`mi_readdxf` raises an advisory, not an error**, for any endpoint not
  shared with another entity ("There are lines or arcs with Orphaned end
  points"). The import succeeds regardless, but pyfemm turns every FEMM
  message into an exception, so an open shape looks like a failure. The
  helper here tolerates that one message and re-raises anything else.
- **A layer only becomes a group if it is declared in the TABLES section.**
  Classic builds its layer list from `LAYER` table entries and uses each
  layer's index as the group number; an entity's group-8 attribute alone
  finds no match and everything silently lands in group 0.
- **Node counts are not comparable across the two importers unless the
  merge tolerance matches.** Classic merges coincident endpoints during
  import; femmqt takes the tolerance as a parameter. The agreement tests
  compare edges by coordinate, and a separate test pins the merge itself.

**Neither implementation reads `$INSUNITS`.** A 1-unit line from a DXF
declaring inches arrives 1 mm long in a millimetre problem, not 25.4. That
is asserted as CURRENT behaviour rather than left undocumented, so
implementing unit conversion fails this test and makes the change a
deliberate one.

Output: `results/dxf_roundtrip/`.
## file_format_roundtrip_test.py

`FILE_FORMATS.md` documents four formats and the relationships between
them -- `.femx` is a binary cache of `.fem`, `.ansx` of `.ans`'s mesh --
and nothing verified any of it. Both GUIs and all four solvers read and
write these files, so a field written but not read back, or read back in
the wrong order, is a data-loss bug no other test would notice (issue #7).

The contract held to is the one `FILE_FORMATS.md` itself states: opening a
file via its cache and via its text source must produce identical state,
and a cache whose recorded source size/mtime no longer match must be
treated as stale. Comparisons are on PARSED STRUCTURES with float
tolerances, never raw bytes, and a failure names the field that drifted.

**This found a real data-corruption bug**, now fixed in
`femm/FemxFileIO.cpp`:

- `IsDefault` is not a boolean in the classic model. It holds the value
  **2**, because the `.fem` text format packs it as bit 1 of a flags field
  written as `IsExternal + IsDefault` and read back as `(v & 2)` / `(v & 1)`.
  The cache loader restored it as `TRUE` (= 1), which sets the **IsExternal**
  bit instead. A default block label loaded through the cache and re-saved
  therefore came back as an *external* (Kelvin outer-region) label with the
  default flag lost -- silently, with no error. femmqt's independent
  implementation models the same flag as a bool and encodes it correctly,
  so only the classic side was affected.
- `mySideLength` (arc column 7) is a derived rendering value that the text
  format does not actually round-trip: the writer emits it but the reader
  parses only seven arc fields and then sets `mySideLength = MaxSideLength`.
  Restoring the stored value verbatim made a cache load disagree with a text
  load of the same file, so arcs rendered at a different segment count. The
  cache loader now normalises the same way the text path does.

Because of the second point the `.fem` idempotence test compares the SECOND
and THIRD saves rather than the first and second: a freshly drawn arc
carries the constructor default of 1, so the first save records 1 and every
save after a load records `MaxSideLength`. A companion test pins that as the
only non-idempotent column, so it cannot grow quietly.

Output: `results/file_format_roundtrip/` -- the fixture models plus
`file_format_roundtrip.txt`.
## boundary_conditions_test.py

Boundary conditions are the part of a FEM model most likely to be silently
wrong: the solve converges and produces a plausible-looking field whether
or not the condition means what the modeller thought, so a test that only
asserts a number came back cannot tell the difference. Every case here is
therefore checked by AGREEMENT BETWEEN TWO INDEPENDENT FORMULATIONS of the
same physical problem (issue #3).

| Family | Cross-check | Observed |
| --- | --- | --- |
| Open boundary | `mi_makeABC` vs a truncated domain, and vs the analytic dipole far field | 2.74% / 0.50% |
| Kelvin transform | `mi_defineouterspace`/`mi_attachouterspace` vs `makeABC`, both vs `mu0*I/(2a)` | 1.21% |
| Antiperiodic | one-pitch slice vs the middle of a six-bar alternating row | 0.4-1.5% |
| Periodic | one periodic pitch vs two periodic pitches | 0.03-0.10% |
| Periodic vs antiperiodic | one ANTIperiodic bar vs two PERIODIC bars of opposite sign | 0.12-1.45% |
| Dirichlet | prescribed value actually held, in electrostatics / heat / current flow | 0.00% |

Notes on why these particular comparisons:

- **A same-polarity row cannot be checked against a finite multi-bar model**
  the way an alternating one can. With every bar the same sign there is no
  cancellation, so the field of the infinite row does not decay and a
  six-bar model is a genuinely different problem -- measured 10% apart at
  y=8mm growing to 30% at y=18mm. That is physics, not a solver defect. Two
  periodic windows of different width over the same infinite structure ARE
  equivalent, so that is what is compared.
- **One antiperiodic bar against two periodic bars of opposite sign** is the
  strongest statement available about the pair: the same infinite row
  expressed through each condition in turn. It fails if either is wrong,
  and unlike comparing each against its own full model it cannot be
  satisfied by both being wrong the same way. A separate test asserts the
  two conditions do NOT agree on the same one-bar slice (they differ by
  46-88%), which would catch them being wired to the same behaviour.
- **The truncated "ground truth" needs an explicit mesh size.** Left to
  automesh across a large domain the air is far too coarse near a 4mm
  dipole and the reference model becomes the LESS accurate of the two: it
  measured 12.9% off the analytic far field where the ABC model was 0.18%.
  Both models now pin the air mesh, and `belasolv`, which the ticket noted
  had "no test of any kind", is additionally covered by
  `analytic_fields_test.py`.

Output: `results/boundary_conditions/` -- the models plus
`boundary_conditions.txt`, the full cross-check table.
## magnetics_analytic_test.py

The magnetics counterpart. `straight_wire_field_test.py` asserts exactly
one number -- |B| near an infinite straight wire, planar, DC, no materials,
no circuits -- which left the parts of the magnetics path this fork
actually touches unverified numerically (issue #2).

| Case | Closed form | Observed error |
| --- | --- | --- |
| Circular loop, on axis (axisymmetric) | `B = mu0*I/(2a)` | 0.61% |
| Same loop, far field on axis | dipole `mu0*I*a^2/(2*(a^2+z^2)^1.5)` | 0.42% |
| Coax, external inductance | `L = mu0/(2*pi)*ln(b/a)` from the field-energy integral | 0.13% |
| Coax, field between conductors | `B = mu0*I/(2*pi*r)` at three radii | 0.13% |
| Solenoid, on-axis centre (two L/D ratios) | `B = mu0*N*I/sqrt(L^2+4R^2)` | 0.33% / 0.68% |
| Two parallel wires, Lorentz force | `F/L = mu0*I1*I2/(2*pi*d)` | 0.67% |
| Two parallel wires, weighted stress tensor | same, via a different integral | 0.63% |
| Skin effect at 100 kHz | `delta = sqrt(2/(omega*mu*sigma))`, 1/e decay | 7.6% |

Three of these are worth calling out:

- **The axisymmetric code path had no numeric check at all** before this.
  Both loop cases exercise `problemtype("axi")` end to end.
- **The two force routes are cross-checked against each other**, not just
  against the closed form: block integral 11 (steady-state Lorentz) and 18
  (steady-state weighted stress tensor) on the same block agree to 0.04%.
  Two parallel wires are used rather than an iron air gap on purpose -- a
  gap force can only be compared against `B^2*A/(2*mu0)` after accounting
  for fringing and gap meshing, so it would test the discretisation as much
  as the force integral, whereas two wires in air have an exact answer.
- **BH-curve saturation** is asserted on the shape of the response rather
  than a single number, since the curve is library data and not a formula.
  A closed planar picture-frame core (no gap, no free ends, so Amperes law
  around the mean path sets `H = N*I/l` with no demagnetising factor to
  estimate) is driven from 1 to 6000 ampere-turns: `mu_r` collapses from
  13047 to 103 while B rises to 1.92 T and stays physically bounded, and
  FEMM's own reported `Mu1` is checked against the `B/(mu0*H)` derived from
  the same point. This is the check that catches BH-interpolation
  regressions of the kind v2.1.2 shipped, where `mu_r` was effectively 1
  regardless of the curve. Note an axisymmetric model cannot be used here:
  a toroidal winding threads the core around its minor cross-section, which
  is not a figure of revolution.

Output: `results/magnetics_analytic/` -- nine models plus
`magnetics_analytic.txt`, a FEM-vs-closed-form comparison table.
## analytic_fields_test.py

The counterpart to `straight_wire_field_test.py` for the other three
solvers. Until it existed, the straight-wire test was the only one in the
suite that checked a NUMBER against physics, and it covers magnetostatics
only -- `belasolv`, `hsolv` and `csolv` were exercised solely by the Lua
command sweep, which asserts that commands RUN, not that their answers are
right. A sign error or a unit-scaling regression in any of the three would
have shipped green (issue #1).

Each case is built from Python, solved, probed, and compared against a
closed form within 2%:

| Case | Closed form |
| --- | --- |
| Electrostatics, parallel plate | `E = V0/d`; `C = eps0*eps_r*A/d`, cross-checked against `2*W/V0^2` from the stored-energy integral |
| Electrostatics, coaxial (axisymmetric) | `V(r) = V0*ln(b/r)/ln(b/a)`; `E_r = V0/(r*ln(b/a))` |
| Heat flow, 1-D slab | linear `T(x)`; `q = k*dT/L` |
| Heat flow, cylindrical shell (axisymmetric) | `T(r) = Tb + (Ta-Tb)*ln(b/r)/ln(b/a)`; `q_r = k*(Ta-Tb)/(r*ln(b/a))` |
| Heat flow, convection boundary | `q = (Thot-Tinf)/(L/k + 1/h)`; `Tsurface = Tinf + q/h` |
| Current flow, rectangular bar | `R = L/(sigma*W*depth)`; `I = V/R`, cross-checked against `V^2/P` from the real-power integral |

The geometries are chosen so the closed form is EXACT for the modelled
region rather than a large-aspect-ratio approximation -- Neumann side walls
make the fringing-free uniform field exact for the parallel plate, and
`dV/dz = 0` already satisfies the natural boundary condition on the flat
ends of the axisymmetric cases, so there are no end effects. That is what
makes a 2% tolerance meaningful. Worst observed error is 0.12%; most cases
agree to the printed precision.

Two API traps this test flushed out, both of which silently produce a
plausible-looking wrong answer rather than an error:

- **Only current flow takes a frequency argument.**
  `ei_probdef`/`hi_probdef` are `(units, type, precision, depth, minangle)`
  but `ci_probdef` is `(units, type, frequency, precision, depth,
  minangle)`. Calling the 5-argument form shifts every value along, so a
  2mm-deep bar gets solved 30mm deep with the minangle as its depth. The
  Lua command sweep had the same bug and is fixed too.
- **`belasolv` and `csolv` return complex phasors** even at DC, because
  both support frequency-domain analysis. `eo_blockintegral` returns
  `[real, imaginary]` rather than a scalar. The helper here asserts the
  imaginary part is negligible instead of discarding it, since a non-zero
  one would mean the solve was not actually static.

Output: `results/analytic_fields/` -- the four models plus
`analytic_fields.txt`, a FEM-vs-closed-form comparison table.
## copy_redraw_benchmark_test.py

FEMM's magnetics editor redraws the entire drawing (every node, segment,
arc, and block label) on every single edit action, including each
individual "copy" operation (`mi_copytranslate`/`mi_copyrotate`, or the
Edit > Copy dialog in the GUI). On a model that already has many small
features drawn, repeating a copy action several times pays for a full
canvas redraw each time.

This repository adds a custom Lua/scripting command, `mi_setredraw(flag)`
(see `femm/femmeLua.cpp`), that lets a script suspend that redraw around a
batch of edits and force a single refresh at the end instead. The GUI's
Copy/Move dialogs (`FemmeView.cpp`) use the same mechanism internally.
Separately, `CFemmeDoc::EnforcePSLG()` (`femm/MOVECOPY.CPP`), called once
per Copy, used to rebuild the *entire* node/segment/arc/block list from
scratch on every call; it now only re-validates the newly added geometry
(see the incremental `EnforcePSLG(tol, nodeStart, lineStart, arcStart,
blockStart)` overload), since Copy only ever appends to the end of each
list.

This test builds an identical cluttered base model (a grid of small block
labels) twice, times a series of separate `mi_copytranslate` calls against
it (once with FEMM's default per-copy redraw, once with
`mi_setredraw(0)`/`mi_setredraw(1)` wrapped around the batch), and asserts
the suppressed run isn't dramatically slower than the baseline (a loose
regression guard -- the absolute numbers are informational, since CI
runners are too timing-noisy for a strict performance SLA).

Output: `results/copy_redraw_benchmark/copy_benchmark.txt`.

## enforce_pslg_correctness_test.py

Correctness check for the incremental `EnforcePSLG` overload above: copies
a line segment so that it crosses a pre-existing one, then asserts (by
parsing the saved `.fem` file's `[NumPoints]`/`[NumSegments]` counts) that
the intersection is still correctly detected and both lines still get
split, and that a newly copied node that coincides with a pre-existing one
is still correctly merged rather than duplicated.

Output: `results/enforce_pslg_correctness_test/` (the `.fem` model and the
`enforce_pslg_correctness.txt` report).

## lua_command_regression_test.py

A broad regression sweep over FEMM's Lua-scripting command surface, as
exposed by pyfemm (~450 functions across the magnetics `mi_`/`mo_`,
electrostatics `ei_`/`eo_`, heat flow `hi_`/`ho_`, and current flow
`ci_`/`co_` prefixes). For each problem type it runs a realistic workflow
(draw geometry, assign properties, select/copy/move/mirror/scale, mesh,
save, solve, post-process, round-trip via `opendocument`) and records every
single command call as PASS / FAIL / SKIP, so a source change that breaks
the shared node/segment/arc/block-label editing code (used by every problem
type's editor) shows up here even if it wasn't touched directly.

This is a smoke/regression test, not a physics-correctness test -- it
checks that the commands still execute without error, not that the
computed fields are right (see `straight_wire_field_test.py` for that kind
of check).

The sweep runs once per test session (module-scoped fixture); five test
functions then assert against its results, one per command group
(magnetics, electrostatics, heat flow, current flow, standalone).
**Magnetics is a hard, zero-tolerance gate** since it's the editor this
fork modifies. A documented `KNOWN_ISSUES` set (see the top of the file)
tolerates a handful of pre-existing, unrelated failures in the other
groups so CI doesn't go red on every run for issues this fork didn't
introduce -- any *new* failure not in that set still fails the build.

Output: `results/lua_command_regression/lua_command_regression.txt` (full
report: summary counts, FAIL details with `[KNOWN ISSUE]` tags, SKIP
details with reasons, the list of pyfemm functions this sweep doesn't
exercise, and the full call log), plus the generated `.fem`/`.dxf`/`.bmp`
models for each problem type.

As of the last run: 561 calls attempted, 544 pass, 2 fail (both in
`KNOWN_ISSUES`, and both are bugs in the `pyfemm` package rather than in
FEMM), 15 skip (commands that open a blocking modal dialog, plus a few
pyfemm does not wrap). All four problem types now solve end-to-end and
all four are hard, zero-tolerance gates.

This is up from 397 pass / 13 fail / 139 skip. Three fixes got there,
all recorded in issues #4, #5 and #6:

1. Each physics has its own input extension and its solver appends that
   extension to the basename it is handed -- `fkn` reads `.fem`,
   `belasolv` `.fee`, `hsolv` `.feh`, `csolv` `.fec`. The sweep saved
   every type as `.fem`, so three of the four solvers looked for a file
   that was not there and exited 7, which the editor reports as the
   generic "problem loading input file". Every post-processing command
   for those types was then skipped -- that alone was most of the 139
   skips.
2. Heat flow then solved, but did not converge: the shipped `Air` heat
   material carries an 18-point temperature-dependent conductivity table
   starting at 200K, and the model fixed its boundary at 0K, so the
   nonlinear iteration chased an extrapolated conductivity. Measured
   before the fix: `hsolv` burning 24,410s of CPU over 7.3 hours on a
   10x10mm square. The reference temperature is now 300K, inside the
   table, and it converges in seconds. `analyze` also runs under a
   120s watchdog now, so a non-converging solve fails the suite instead
   of hanging it.
3. All four edges carried the same zero-valued boundary condition, so
   the solved field was identically zero. That cannot distinguish a
   correct answer from a stream of zeros, and for current flow it made
   `co_blockintegral` return a literal `nan` (0/0) which pyfemm eval()s
   into "name 'nan' is not defined". One edge is now driven, so each
   solve has a real gradient.

The known, pre-existing findings:

- `AWG`/`IEC` (wire-gauge helper functions) fail with `name 'exp' is not
  defined` -- a bug in the installed `pyfemm` PyPI package itself (missing
  `math.exp` import), unrelated to this fork.
- **`*i_savebitmap` was NOT a bug in the input editors' bitmap capture --
  it is a `pyfemm` bug, now worked around here (issue #4).** The four
  input-editor wrappers (`mi_`, `ei_`, `hi_`, `ci_savebitmap`, and
  `*i_savemetafile` too) omit the `fixpath()` call that their `*o_`
  counterparts and `*i_saveas` all make, so a Windows path reaches Lua with
  its backslashes intact and they are eaten as string escapes; the file
  then cannot be created. That is the whole reason `mo_savebitmap` "worked
  fine" on the same session -- `mo_` calls `fixpath`, `mi_` does not. The
  sweep now passes a forward-slash path and all four assert normally.
  Fixing `pyfemm` itself is out of scope (it is pip-installed), but any
  script hitting this should pass forward slashes to `*i_savebitmap`.
  The capture path itself had two real defects, both fixed in
  `femm/BitmapCapture.h/.cpp`: a never-shown view reports a zero-height
  client rect, which `CreateCompatibleBitmap` answers with a 1x1
  MONOCHROME bitmap, so savebitmap used to write a valid, useless 58-byte
  file and report success; and seven of the eight copies leaked a DC per
  call and deleted a bitmap still selected into one.
- The electrostatics/heat-flow/current-flow problem types' `analyze` step
  ("problem loading input file") doesn't yet succeed with this script's
  property setup, so their post-processing commands are skipped
  (cascading from that). Their geometry/property/edit/view/mesh commands
  all pass; only the solve step and everything downstream of it is
  affected. Not investigated further here since this fork doesn't modify
  those editors -- a good next step if extending this suite.
