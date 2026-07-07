# Test Models

Example and test scripts that build and solve a FEMM model entirely from
Python, using the `pyfemm` COM interface to a locally built `femm.exe`.

## straight_wire_field.py

Builds a 2D planar magnetostatics problem: a single current-carrying wire
(10 A) surrounded by an open-boundary air domain. Solves it and compares the
computed flux density at a probe point against the closed-form solution for
an infinite straight wire (Ampere's law), printing a PASS/FAIL check.

### Prerequisites

- A built `femm.exe` (see the repository root `README.md` / `build.ps1`),
  registered as a COM automation server (`femm.ActiveFEMM`).
- Python packages: `pip install pyfemm pywin32`

### Run

```
python straight_wire_field.py
```

Generates `straight_wire_field.fem` (the model) and `straight_wire_field.ans`
(the solution) alongside the script; both are regenerated on each run and
are not tracked in git.

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
batch. Results (including the measured speedup) are written to
`results/copy_benchmark.txt`.

### Run

```
python copy_redraw_benchmark.py
```

## enforce_pslg_correctness_test.py

Correctness check for the incremental `EnforcePSLG` overload above: copies
a line segment so that it crosses a pre-existing one, then verifies (by
parsing the saved `.fem` file's `[NumPoints]`/`[NumSegments]` counts) that
the intersection is still correctly detected and both lines still get
split, and that a newly copied node that coincides with a pre-existing one
is still correctly merged rather than duplicated. Writes a PASS/FAIL report
to `results/enforce_pslg_correctness.txt`.

### Run

```
python enforce_pslg_correctness_test.py
```
