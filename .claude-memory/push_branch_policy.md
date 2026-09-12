---
name: push-branch-policy
description: "In femmx (SpgV0/femmx), work and push go to new_features as of 2026-09-12; main is no longer the working branch"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 0645e6ab-f4a7-4004-a39e-44c28675f293
  modified: 2026-09-12T00:00:00.000Z
---

**Current rule (2026-09-12): work on `new_features` and push there.**
The user reversed the main-only rule with: "Continue work on
new_features branch instead of main and push changes there from now on."
`new_features` was recreated from `main` at commit ed2c2ed and pushed;
local `main` was moved back to `origin/main` so the branch is the only
place the newer work lives.

**Superseded rule (2026-09-09 to 2026-09-12): push to `main` only.** The
user had collapsed the branch model -- `new_features` and `rc` merged
into `main` and deleted from `origin` -- saying "From now on, you will be
only pushing to main." Recorded because it explains why `new_features`
had to be recreated rather than simply checked out, and because a rule
that reversed once can reverse again: check this file rather than
assuming.

**Why the original three-branch flow went away:** `new_features` -> `rc`
-> `main` existed to keep `main` as a clean rehosted upstream baseline
pending review. That baseline is long gone, so `rc` never held anything
`main` didn't. `rc` has NOT been recreated and should not be without the
user asking.

**How to apply:** commit finished, verified work to `new_features` and
push it there. Leave `main` alone unless the user asks for a merge or a
release. Do not invent additional feature branches as a substitute.

**Push cadence (carried over, still applies):** push at least once a day
per working session as a work backup — long sessions otherwise leave many
hours of finished work existing only on this machine. Push verified work
only, not mid-edit or broken states; "backup" doesn't mean push on every
keystroke. Note each push triggers `.github/workflows/ccpp.yml`, so batch
a session's commits into one push rather than pushing per commit.

**Repo note (2026-07-08):** The project was ported from its earlier
GitHub host to `https://github.com/SpgV0/femmx` — this is now the sole
`origin` (with `upstream` = `cenit/FEMM`, read-only). The old host's
credential was explicitly cleared from this machine's credential manager
and replaced with `SpgV0`'s — don't try to push to the old repo/account,
and don't re-add it as a remote. `master` was deleted from the remote
(2026-07-17); `main` is the default and `origin/HEAD` points there. See
[[gpu-speedup-investigation]] for the first feature shipped under the new
repo, and [[release_tagging_workflow]] for the release sequence.
