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
