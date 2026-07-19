---
name: validate-numerical-ports-empirically
description: "When porting a numerical/physics formula from femm's classic MFC source (especially anything touching LengthConv/unit conversions), don't trust a from-scratch re-derivation -- validate against the classic GUI's own displayed output on a real file"
metadata:
  type: feedback
  originSessionId: 846a52dc-e5cc-4b0f-9a4f-7b5debeae297
  modified: 2026-07-19T16:00:49.356Z
---

When implementing a numerical/physics feature in `femmqt` (or anywhere)
that's meant to reproduce a classic FEMM (`femm/`) calculation, prefer
one of two approaches over "this looks dimensionally sensible, I derived
it myself": (a) a literal, line-by-line port of the actual classic
source, or (b) empirical validation against the classic GUI's own output
on a real solved file. Do both when the stakes are real (a wrong
engineering number is worse than no feature).

**Why**: while implementing Circuit Props for [[femmqt_qt_gui]] (Round
6), a from-first-principles re-derivation of the solid-conductor
voltage/current relationship (Kirchhoff's current law over a block,
solving for the unknown terminal voltage) was WRONG -- off by exactly
the length-conversion factor (1000x for a millimeters-unit file). The
bug: classic FEMM's `LengthConv[LengthUnits]` usage is genuinely
*inconsistent* across related quantities in the same calculation --
`Depth` gets converted to meters in the final voltage formula, but the
raw per-block `dVolts` value (read from `.ans`) is used unconverted in
that same formula. This isn't a typo; it's really how the classic code
works, confirmed by reading it line by line. A "cleaned up," internally-
consistent re-derivation will get this wrong precisely because it's
*more* consistent than the thing it's supposed to reproduce.

**How it was caught**: opened the same real solved `.ans` file in both
GUIs -- the classic one (`bin/plain/femmx.exe`, launched and driven via
the same pywinauto/win32 UI-automation technique used elsewhere this
session) and the in-progress Qt port -- and compared displayed numbers
directly. A standalone Python script re-implementing the candidate
formula against the raw `.ans` data made it fast to iterate before
touching the actual Qt/C++ code.

**How to apply**: any time a ported calculation involves `LengthConv`,
unit conversions, or other classic-FEMM internal conventions, budget time
to open the corresponding real test file in the classic GUI (`bin\`,
`bin\plain\`, or a fresh build) and compare against its actual displayed
output before considering the port done -- not just "the code compiles
and looks plausible." This generalizes beyond Circuit Props to any future
`femm/FemmviewDoc.cpp`/`femm/FemmeDoc.cpp` post-processing port.
