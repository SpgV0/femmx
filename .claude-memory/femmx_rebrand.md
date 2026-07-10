---
name: femmx-rebrand
description: "Project renamed femm_plus -> femmx: main exe is femmx.exe, repo is SpgV0/femmx; COM ProgID and solver exe names unchanged"
metadata:
  node_type: memory
  type: project
  originSessionId: 0645e6ab-f4a7-4004-a39e-44c28675f293
---

On 2026-07-09 the user renamed the fork from femm_plus to **FEMMX**
(repo: `https://github.com/SpgV0/femmx`).

**What changed:**
- Main GUI/COM-server executable: `femm.exe` -> `femmx.exe`
  (`femm/CMakeLists.txt`'s `project()`/`add_executable()` target).
- Window title, VERSIONINFO strings (`femm/femm.rc`), installer
  (`script.nsi`), CI workflow/artifact names
  (`.github/workflows/ccpp.yml`), `scripts/register_femm_com.ps1`'s
  default path, and the Mathematica/Octave interface scripts
  (`mathfemm/mathfemm.m`, `mathfemm/usage.nb`,
  `octavefemm/mfiles/openfemm.m`) all updated to reference `femmx.exe`.
- Manual title page (`manual/manual.tex.in`) now leads with "FEMMX".
- All source comments/docs/messages saying `femm_plus` -> `femmx`.
- Removed the `spgryparis/femm_mods` GitHub reference from
  `README.md`/`NOTICE.md` per explicit request -- the changelog/notice
  now only names the original `cenit/FEMM` upstream and the current
  `SpgV0/femmx` hosting, not the intermediate fork host.

**What did NOT change** (explicit user decisions, asked via
AskUserQuestion before starting):
- The COM automation ProgID stays `femm.ActiveFEMM` -- pyfemm/win32com
  scripts, this repo's whole test suite, and `test/conftest.py`'s
  registry check all still work unmodified. Only the `LocalServer32`
  registry value (the path `register_femm_com.ps1` writes) now points
  at `femmx.exe` instead of `femm.exe`.
- The four solver executables (`fkn.exe`, `csolv.exe`, `hsolv.exe`,
  `belasolv.exe`) and `triangle.exe` keep their names -- only the main
  `femm.exe` was in scope.
- The `femm/` source directory and its file names (`femm.h`,
  `femm.cpp`, `femm.rc`, ...) are unchanged; only the CMake
  target/output name changed. Similarly `femm.odl`'s `library Femm`
  type-library name/GUIDs are untouched (COM identity stability).
- `CompanyName "David Meeker"` in the VERSIONINFO block and
  `SetRegistryKey("Gedanken Magnetics\\FEMM\\4")` in `femm/femm.cpp`
  (the app's settings/MRU registry path) were deliberately left alone
  -- original-author attribution, not fork branding.

See [[gpu-speedup-investigation]] for the AC/harmonic GPU work shipped
the same session, and [[push_branch_policy]] for the repo/credential
history this rebrand builds on.
