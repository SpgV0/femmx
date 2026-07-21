---
name: qt-gui-scope-deferred
description: "User wants new physics-type work (heat/electrostatics/current-flow) done in the classic MFC GUI + solvers first, not the Qt GUI, which stays magnetics-only for now"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 846a52dc-e5cc-4b0f-9a4f-7b5debeae297
  modified: 2026-07-21T11:17:34.978Z
---

When adding a feature for a non-magnetics physics type (heat flow,
electrostatics, current flow), implement it in the classic MFC GUI
(`femm/`) and the relevant solver, not [[femmqt_qt_gui]] (which per its
own plan is deliberately magnetics-only, Phase 1 MVP).

**Why:** stated directly, 2026-07-21: "Do not bother with the qt
application for the other problem types yet, just focus on the old gui
and the solver accelerations." This came mid-turn while I was mid-
investigation for the hsolv (heat-flow) CUDA port -- confirms the
existing plan's magnetics-only Qt scope is intentional and current, not
just an unfinished backlog item to pick up opportunistically.

**How to apply:** default new non-magnetics feature work to classic
GUI + solver only. Only extend femmqt to a new physics type if the user
explicitly asks for it.
