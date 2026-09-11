# Manual checks for the scifemm and mathfemm bridges

Added for issue #20.

Three of FEMMX's four scripting bridges cannot be exercised on a CI
runner. This file is what stands in for that, and it is deliberately a
procedure rather than a promise: a test that skips forever tells nobody
how to check by hand.

`test/bridge_wrappers_test.py` covers what *can* be automated for all
three — every command name a wrapper builds must correspond to a command
`femmx.exe` registers. That catches the failure these bridges actually
suffer from (a renamed or never-registered command) without needing any
interpreter installed. What it cannot check is whether the transport
still works: whether Scilab can still load `scilink.dll`, whether
Mathematica's MathLink still connects.

## Why each one is manual

| Bridge | Automatable? | Why not |
| --- | --- | --- |
| `pyfemm` | yes, extensively | every other test in `test/` |
| `octavefemm` | best effort | needs `actxserver`, from the long-unmaintained Octave-Forge `windows` package; its fallback path writes to a hardcoded `c:/FEMMX/bin/` and so only works against an *installed* copy, never the build tree |
| `scifemm` | no | loads a compiled `scilink.dll` through Scilab's `link()`; Scilab is not on the runners |
| `mathfemm` | no | Mathematica is commercial and cannot be installed on a runner |

## scifemm

Needs Scilab (tested against 6.x) and an installed FEMMX, because
`scifemm.sci` resolves `scilink.dll` relative to its own path.

1. Start Scilab. `exec('<repo>/scifemm/scifemm.sci', -1);`
   — **expect** no error. A failure here is usually `scilink.dll` built
   for the wrong architecture: it must match the Scilab build (64-bit).
2. `openfemm();`
   — **expect** a FEMMX window. This is the transport check the static
   test cannot do: it exercises `link()`, `call2femm` and the COM
   handshake in one go.
3. ```
   newdocument(0);
   mi_probdef(0,'millimeters','planar',1e-8,0,30);
   mi_addnode(0,0); mi_addnode(10,0);
   mi_addsegment(0,0,10,0);
   ```
   — **expect** two nodes and a segment drawn in the editor.
4. `mi_saveas('c:/temp/scifemm_check.fem');`
   — **expect** the file exists and contains `[NumSegments] = 1`.
5. `closefemm();`
   — **expect** the process exits. Check Task Manager: a leftover
   `femmx.exe` means the bridge's teardown is broken, which is the same
   defect class `test/com_automation_test.py` covers for pyfemm.

## mathfemm

Needs Mathematica and an installed FEMMX.

1. `<< "<repo>/mathfemm/mathfemm.m"` — **expect** no messages.
2. `OpenFEMM[]` — **expect** a FEMMX window. This is the MathLink check.
3. ```
   NewDocument[0];
   MIProbDef[0,"millimeters","planar",1.*^-8,0,30];
   MIAddNode[0,0]; MIAddNode[10,0];
   MIAddSegment[0,0,10,0];
   MISaveAs["c:/temp/mathfemm_check.fem"];
   ```
   — **expect** the file exists and contains `[NumSegments] = 1`.
4. `CloseFEMM[]` — **expect** no leftover process.

`mathfemm/usage.nb` is the upstream notebook and is a longer, if
unmaintained, exercise of the same surface.

## Known drift, not fixed here

`test/bridge_wrappers_test.py` records per-bridge coverage in its report
(`test/results/bridge_wrappers/bridge_wrappers.txt`) rather than
asserting parity, because the three bridges were written at different
times and none has ever been regenerated. Two differences are worth
knowing about:

- **The FEMMX-specific commands reach Octave only.** `*_setredraw`,
  `*_setgpuaccel`, `setgui`/`getgui`, `mi_savepng`/`mo_savepng` and
  `get_solve_stats` have `octavefemm` wrappers; `scifemm` and `mathfemm`
  have none, so a Scilab or Mathematica user cannot reach this fork's own
  additions at all.
- **`mathfemm` lags by roughly ninety commands**, most of the current-flow
  editor and post-processor among them.

Neither is a broken wrapper — they are missing ones, which fail loudly at
the call site rather than silently — so the static test does not fail on
them.
