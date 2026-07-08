---
name: push-branch-policy
description: "In femm_mods, push new work to the new_features branch, not main, for later review"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 0645e6ab-f4a7-4004-a39e-44c28675f293
---

Do not push commits directly to `main` in the `femm_mods` repo
(https://github.com/spgryparis/femm_mods). Create/use a `new_features`
branch (or another appropriately named feature branch) and push there
instead.

**Why:** The user wants changes reviewed before they land on `main` —
`main` should stay as the clean rehosted baseline from upstream FEMM.

**How to apply:** Before any `git push`, check the current branch. If it's
`main`, create or switch to `new_features` (or a more specific feature
branch if the work is unrelated to prior new_features commits) and push
that instead. Only push to `main` if the user explicitly asks for it in a
given request.
