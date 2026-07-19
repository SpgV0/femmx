---
name: build-and-com-registration-gotchas
description: "Two build/test gotchas that cost significant time this session: bare build.ps1 hangs forever non-interactively on failure, and the COM-registered femmx.exe isn't the one bare build.ps1 produces"
metadata: 
  node_type: memory
  type: project
  originSessionId: 846a52dc-e5cc-4b0f-9a4f-7b5debeae297
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
