---
name: push-branch-policy
description: "In femm_plus (formerly femm_mods), commit to new_features locally but hold off pushing to origin until the user explicitly says to"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 0645e6ab-f4a7-4004-a39e-44c28675f293
---

Do not push commits directly to `main` in this repo. Create/use a
`new_features` branch (or another appropriately named feature branch) and
push there instead.

**Why:** The user wants changes reviewed before they land on `main` —
`main` should stay as the clean rehosted baseline from upstream FEMM (see
the `femm-4.2-22Oct2023` tag marking that exact pre-fork state).

**How to apply:** Before any `git push`, check the current branch. If it's
`main`, create or switch to `new_features` (or a more specific feature
branch if the work is unrelated to prior new_features commits) and push
that instead. Only push to `main` if the user explicitly asks for it in a
given request.

**Update (2026-07-09): hold pushes until told.** The user asked to keep
committing locally on `new_features` as normal, but NOT run `git push` to
`origin` until they explicitly say so (e.g. "push now" / "go ahead and
push") — each push triggers the `.github/workflows/ccpp.yml` CI build,
and they want to batch work rather than trigger a build per commit.
**How to apply:** keep making local commits as work finishes (that's
still expected), but stop before the `git push` step and tell the user
what's queued locally instead of pushing automatically. Push only once
they give explicit go-ahead in that session.

**Repo note (2026-07-08):** The project was ported from
`spgryparis/femm_mods` to `https://github.com/SpgV0/femm_plus` — this is
now the sole remote (`origin`), with the same two branches and history
(the `new_features`-only commits were re-authored to `SpgV0
<spgr.eng.v0@gmail.com>` during the port; `main`'s original upstream
history is untouched). The old `spgryparis/femm_mods` credential was
explicitly cleared from this machine's credential manager and replaced
with `SpgV0`'s — don't try to push to the old repo/account, and don't
re-add it as a remote. See [[gpu-speedup-investigation]] for the first
feature shipped under the new repo.
