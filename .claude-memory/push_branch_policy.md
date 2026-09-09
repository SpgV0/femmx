---
name: push-branch-policy
description: "In femmx (SpgV0/femmx), main is the only branch — commit and push directly to main; the old new_features -> rc -> main flow is retired"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 0645e6ab-f4a7-4004-a39e-44c28675f293
  modified: 2026-09-09T00:00:00.000Z
---

**Current rule (2026-09-09): push to `main`, and only `main`.** The user
collapsed this repo's branch model — `new_features` and `rc` were merged
into `main` and deleted, locally and on `origin` — and stated directly:
"From now on, you will be only pushing to main."

**Why:** the three-branch flow (`new_features` -> `rc` -> `main`) existed
to keep `main` as a clean rehosted upstream baseline pending review. That
baseline is long gone — `main` has carried the fork's own work through
v2.1.2 — so the extra branches were pure ceremony: `rc` never held
anything `main` didn't, and every release merged the same commits twice.

**How to apply:** commit finished, verified work straight to `main` and
push it there. Do not recreate `new_features` or `rc`, and do not invent a
feature branch as a substitute for the retired flow. Create a
purpose-named branch only when the user asks for one, or for genuinely
speculative work that shouldn't sit on `main` (e.g. the surviving
`csr-spmv-wip`) — and say so rather than doing it silently.

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
