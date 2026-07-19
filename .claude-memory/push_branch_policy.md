---
name: push-branch-policy
description: "In femmx (formerly femm_mods, then femm_plus), commit to new_features locally but hold off pushing to origin until the user explicitly says to"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 0645e6ab-f4a7-4004-a39e-44c28675f293
  modified: 2026-07-19T07:19:54.857Z
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

**Update (2026-07-19): superseded by a daily push cadence.** The user
asked to push to `new_features` on `origin` once a day, explicitly framed
as backing up work ("push... for backing up your work"), during a long
session building a large new feature (a Qt-based GUI, femmqt/) entirely
in the working tree with nothing pushed for many hours.
**Why:** long sessions can accumulate a large amount of uncommitted/
unpushed work that only exists on this one machine — a daily push is
insurance against losing it, separate from the earlier "batch it up"
preference about not spamming CI.
**How to apply:** once per day (per session/day of work, not literally
every 24h if idle), commit locally-completed, verified work on
`new_features` and push to `origin` without waiting for an explicit
"push now" — this takes priority over the 2026-07-09 "hold until told"
default. Still only ever push `new_features` (never `main` without
explicit instruction, see above), and still only push work that's been
verified/tested, not mid-edit/broken states — "backup" doesn't mean
push-on-every-keystroke, it means don't let a full day of finished,
working progress sit unpushed.

**Repo note (2026-07-08):** The project was ported from its earlier
GitHub host to `https://github.com/SpgV0/femmx` — this is now the sole
remote (`origin`), with the same two branches and history (the
`new_features`-only commits were re-authored to `SpgV0
<spgr.eng.v0@gmail.com>` during the port; `main`'s original upstream
history, cloned from `cenit/FEMM`, is untouched). The old host's
credential was explicitly cleared from this machine's credential
manager and replaced with `SpgV0`'s — don't try to push to the old
repo/account, and don't re-add it as a remote. See
[[gpu-speedup-investigation]] for the first
feature shipped under the new repo.
