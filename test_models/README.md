# Test Models

Example and test scripts that build and solve a FEMM model entirely from
Python, using the `pyfemm` COM interface to a locally built `femm.exe`.

Every script writes its generated files (models, solutions, reports) under
`results/<script_name>/`. The `.txt` reports are tracked in git as evidence
of the last run; model/binary artifacts (`.fem`, `.ans`, `.res`, `.dxf`,
`.bmp`, mesh intermediates) are regenerated on each run and gitignored.

### Prerequisites (all scripts)

- A built `femm.exe` (see the repository root `README.md` / `build.ps1`),
  registered as a COM automation server (`femm.ActiveFEMM`).
- Python packages: `pip install pyfemm pywin32`

## straight_wire_field.py

Builds a 2D planar magnetostatics problem: a single current-carrying wire
(10 A) surrounded by an open-boundary air domain. Solves it and compares the
computed flux density at a probe point against the closed-form solution for
an infinite straight wire (Ampere's law), printing a PASS/FAIL check.

```
python straight_wire_field.py
```

Output: `results/straight_wire_field/straight_wire_field.{fem,ans}`.

## copy_redraw_benchmark.py

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

`copy_redraw_benchmark.py` builds an identical cluttered base model (a grid
of small block labels) twice, then times a series of separate
`mi_copytranslate` calls against it: once with FEMM's default per-copy
redraw, and once with `mi_setredraw(0)`/`mi_setredraw(1)` wrapped around the
batch.

```
python copy_redraw_benchmark.py
```

Output: `results/copy_redraw_benchmark/copy_benchmark.txt`.

## enforce_pslg_correctness_test.py

Correctness check for the incremental `EnforcePSLG` overload above: copies
a line segment so that it crosses a pre-existing one, then verifies (by
parsing the saved `.fem` file's `[NumPoints]`/`[NumSegments]` counts) that
the intersection is still correctly detected and both lines still get
split, and that a newly copied node that coincides with a pre-existing one
is still correctly merged rather than duplicated.

```
python enforce_pslg_correctness_test.py
```

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
computed fields are right (see `straight_wire_field.py` for that kind of
check).

```
python lua_command_regression_test.py
```

Output: `results/lua_command_regression/lua_command_regression.txt` (full
report: summary counts, FAIL details, SKIP details with reasons, the list
of pyfemm functions this sweep doesn't exercise, and the full call log),
plus the generated `.fem`/`.dxf`/`.bmp` models for each problem type.

Coverage is most rigorous for **magnetics** (`mi_`/`mo_`), since that is
the editor this fork actually modifies. As of the last run: 549 calls
attempted, 397 pass, 13 fail, 139 skip (mostly cascading skips when a
problem type's solve didn't succeed, plus a handful of commands that open
blocking modal dialogs and can't run unattended). Known, pre-existing
findings surfaced by this sweep (not regressions from this fork's
changes -- magnetics itself passes cleanly apart from the first item):

- `mi_savebitmap`/`ei_savebitmap`/`hi_savebitmap`/`ci_savebitmap` reliably
  fail with `"Critical error on getting bmp info, possible page fault
  ahoyN"` when called on the pre-processor (input) editor's view. The
  post-processor equivalent (`mo_savebitmap`) works fine, so this looks
  like a genuine, narrow, pre-existing bug in the input editors' bitmap
  capture, not something this fork's changes touch.
- `ci_refreshview` is exposed as a Python wrapper by pyfemm but isn't
  actually a registered Lua command in the current-flow editor
  (`"attempt to call global 'ci_refreshview' (a nil value)"`).
- `AWG`/`IEC` (wire-gauge helper functions) fail with `name 'exp' is not
  defined` -- a bug in the installed `pyfemm` PyPI package itself (missing
  `math.exp` import), unrelated to this fork.
- The electrostatics/heat-flow/current-flow problem types' `analyze` step
  ("problem loading input file") doesn't yet succeed with this script's
  property setup, so their post-processing commands are skipped
  (cascading from that). Their geometry/property/edit/view/mesh commands
  all pass; only the solve step and everything downstream of it is
  affected. Not investigated further here since this fork doesn't modify
  those editors -- a good next step if extending this suite.
