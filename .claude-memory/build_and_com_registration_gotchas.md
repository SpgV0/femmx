---
name: build-and-com-registration-gotchas
description: "Two build/test gotchas that cost significant time this session: bare build.ps1 hangs forever non-interactively on failure, and the COM-registered femmx.exe isn't the one bare build.ps1 produces"
metadata: 
  node_type: memory
  type: project
  originSessionId: 846a52dc-e5cc-4b0f-9a4f-7b5debeae297
  modified: 2026-07-19T22:16:37.734Z
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
