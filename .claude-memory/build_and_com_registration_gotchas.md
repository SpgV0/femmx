---
name: build-and-com-registration-gotchas
description: "Two build/test gotchas that cost significant time this session: bare build.ps1 hangs forever non-interactively on failure, and the COM-registered femmx.exe isn't the one bare build.ps1 produces"
metadata: 
  node_type: memory
  type: project
  originSessionId: 846a52dc-e5cc-4b0f-9a4f-7b5debeae297
  modified: 2026-07-20T11:13:08.618Z
---

**`build.ps1` hangs forever if run non-interactively without
`-DisableInteractive`, on ANY failure.** Its error path (`MyThrow`)
falls back to `Write-Host "Press any key to continue..."` +
`$Host.UI.RawUI.ReadKey(...)`, which blocks forever in a background/
headless process — there's no console attached to send a keypress to.
**Why this matters:** a background build I launched this session sat
for ~40 minutes doing nothing after a LaTeX manual sub-step failed;
the actual failure took seconds, the rest was pure hang.
**How to apply:** always pass `-DisableInteractive` (and usually
`-DisableLaTeX`, since the manual build is optional and this session
found it's fragile in CI too — see the `ccpp.yml` PATH-fix commit) when
invoking `build.ps1` directly or in the background. Prefer
`build_plain.bat`/`build_cuda.bat` instead when a real dev build is
needed (see next item) — they already wrap `build.ps1` with these flags
via `build_femmx.ps1`.

**The COM-registered `femmx.exe` is `bin\plain\femmx.exe` (or
`bin\cuda\femmx.exe`), NOT the top-level `bin\femmx.exe` that a bare
`build.ps1` run produces.** `build_femmx.ps1` (invoked by
`build_plain.bat`/`build_cuda.bat`) wraps `build.ps1` and then *moves*
everything `build.ps1` just placed in `bin\` into `bin\plain\` or
`bin\cuda\`, so a plain build and a CUDA build don't clobber each
other. The Windows registry's `femm.ActiveFEMM` COM class
`LocalServer32` key points at one of those variant subfolders (whichever
was registered last, typically via `scripts/register_femm_com.ps1`).
**Why this matters:** a bare `build.ps1` run silently produces a build
that pyfemm/COM automation never actually launches — testing against it
looks fine (it compiles) but any runtime verification via
`femm.openfemm()` exercises a *stale* binary. This cost real time this
session: an interactive-dark-mode fix appeared broken via automated
screenshot testing purely because the freshly-built exe wasn't the one
COM was launching.
**How to apply:** for any change that needs runtime verification via
pyfemm/COM automation (not just a compile check), always rebuild via
`build_plain.bat` (or `build_cuda.bat`), never bare `build.ps1` — and
check `bin\plain\femmx.exe`'s timestamp actually moved before trusting
a test result. If genuinely unsure which exe COM will launch, check the
registry directly: `Get-ItemProperty
"Registry::HKEY_CLASSES_ROOT\CLSID\<clsid-from-femm.ActiveFEMM>\LocalServer32"`.

**Killing a `build_plain.bat`/`build_cuda.bat` run mid-compile leaves
`bin\` contaminated, and the next build (even a different variant)
silently inherits the mess.** `build.ps1` writes straight into
top-level `bin\` throughout the build (CUDA runtime DLLs in particular
get staged there early, well before final linking, once
`-DENABLE_CUDA_SOLVER=ON`) and only moves things into `bin\plain\`/
`bin\cuda\` at the very end, on success. Killing the process (e.g. via
`TaskStop` on a background task) skips that move entirely and leaves
whatever was already written sitting in top-level `bin\` indefinitely —
there's no cleanup-on-interrupt. **Why this matters:** confirmed
directly (2026-07-19/20 session) — a killed `build_cuda.bat` left
~900MB of `cudart64_*.dll`/`cublas64_*.dll`/etc. in top-level `bin\`;
the *next* `build_plain.bat` run's move-to-`bin\plain\` step swept them
in too, producing a 664MB "plain" installer (should be ~32MB) that
bundled CUDA libraries a CPU-only build has no business shipping, and
separately made `test/gpu_solver_test.py`'s CUDA-detection heuristic
(which globs top-level `bin\`, see [[gpu_speedup_investigation]]) wrongly
flag the plain build as CUDA-capable, causing a spurious speedup-test
failure against a build that was never actually GPU-accelerated.
**How to apply:** after killing any `build_plain.bat`/`build_cuda.bat`
run, run `git clean -ndx bin/` (dry run) then `git clean -fdx bin/`
before the next build — safe in this repo specifically because the 6
static data files (`condlib.dat`, `heatlib.dat`, `init.lua`,
`license.txt`, `matlib.dat`, `statlib.dat`) are git-tracked and `git
clean` never touches tracked files, so this wipes exactly the build
output and nothing else. Never assume a killed build left `bin\` as it
was before — verify with `du -sh bin/*` if anything looks oversized.

**A missing runtime DLL for a Qt6 app doesn't always fail loudly —
`script.nsi`'s individually-named `Qt6*.dll` File lines silently drifted
out of sync and caused a real, hard-to-diagnose bug (2026-07-20
session).** `Qt6PrintSupport.dll` (needed once Print/Print Preview was
added to femmqt) was never added to that hand-maintained list, so every
installed copy shipped without it. **Symptom was NOT a loud "DLL not
found" dialog** — `femmqt.exe` would start, successfully load 41 other
DLLs, then hang indefinitely: 0% CPU (confirmed via `Get-Process ...`
CPU delta over several seconds — a real deadlock, not a spin-loop or
slow AV scan), zero windows ever created (`EnumWindows` filtered to the
PID returned nothing), `qwindows.dll` (the Qt platform plugin) never
even appeared in the loaded-module list. This is a materially different
failure mode than a direct/implicit dependency going missing (which
Windows' loader normally refuses to start the process for, with an
immediate, loud "code execution cannot proceed" dialog) — worth
remembering that a *transitively*-needed DLL for Qt's plugin-loading
machinery can instead manifest as a silent hang.
**Diagnostic technique that found it, in order:** (1) reproduce with the
exact installed binary, not just the dev-tree copy — the same exe ran
fine from the repo's `bin\cuda\` but hung from `C:\FEMMX\bin\`, which is
what proved it was a packaging gap, not a code bug; (2)
`(Get-Process ...).Modules.Count` polled twice a few seconds apart
distinguishes "still initializing" from "genuinely stuck" — ours never
moved past 41; (3) `Get-Process ... CPU` delta over a few seconds at 0
confirms a true blocking wait, not a spin-loop, which would instead show
rising CPU; (4) `Compare-Object` on `Get-ChildItem -Recurse -File` file
lists between a known-working directory and the broken one is the
fastest way to spot "one file is just missing" once you suspect
packaging rather than logic; (5) `dumpbin /dependents` (MSVC toolset,
`VC\Tools\MSVC\<ver>\bin\Hostx64\x64\dumpbin.exe`) on both the plugin
DLL and the suspected-missing DLL to rule out *their* transitive deps
being the real gap, before testing the fix by just copying the one file
in and relaunching.
**Also found while testing this**: a genuinely misleading side effect —
after ~13 failed test launches (from before the fix) each left an
undismissed native "`<exe>.exe` - System Error" dialog on screen (owned
by `csrss.exe` on the OS's behalf, not by the failed app's own PID,
confirmed via `EnumWindows` + `GetWindowThreadProcessId` — these are
OS-level process-launch-failure notices, not owned by the never-fully-
created application process), a *later*, genuinely-successful launch's
screenshot (a screen-region capture, not a true per-window capture) can
visually include one of those stale overlapping dialogs, making a
working fix look broken in the screenshot. Always enumerate all windows
titled like an error dialog and check they're actually gone (or note
their real owning PID) before trusting a "it's still broken" screenshot.
**Fix applied**: switched the hand-maintained `Qt6*.dll` File lines in
`script.nsi` to a single `File /nonfatal "bin\Qt6*.dll"` wildcard,
matching what the section's own pre-existing comment already claimed it
did and what every plugin-subfolder `File` line below it already does —
the next new Qt module femmqt links against won't need this list edited
by hand.
