---
name: sync-memory-to-git
description: "Keep .claude-memory/ in the femm_mods repo synced with local memory files, for reuse on other PCs"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 0645e6ab-f4a7-4004-a39e-44c28675f293
---

The user wants this project's Claude memory backed up in git so it can be
reused from another PC. A snapshot lives at `.claude-memory/` in the
`femm_mods` repo (pushed to `new_features`, commit e698a95).

**Why:** Memory normally only exists locally at
`~/.claude/projects/<project-hash>/memory/`, which doesn't transfer to a
different machine or clone path automatically.

**How to apply:** When writing or updating a memory file in this project,
also copy it into `<repo>/.claude-memory/` and commit+push it (same branch
policy as everything else -- [[push-branch-policy]]: new_features, not
main). Don't let the two copies drift silently out of sync.
