# Demo Models

Example scripts that build and solve a FEMM model entirely from Python,
using the `pyfemm` COM interface to a locally built `femm.exe`.

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
