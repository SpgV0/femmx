---
name: close-test-windows-permission
description: "User grants standing permission to close/kill windows and processes I spawned myself during testing in femmx, without asking each time"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 0645e6ab-f4a7-4004-a39e-44c28675f293
---

Standing permission to close windows and terminate processes that I
spawned myself during testing (e.g. femm.exe/fkn.exe/python.exe test
processes left waiting on a GUI window I created), via taskkill or
PostMessage(WM_CLOSE), without asking for confirmation each time.

**Why:** The Claude Code auto-mode security classifier blocks taskkill
and PostMessage/WM_CLOSE targeting PIDs or window handles sourced from
tasklist/window-enumeration output by default (treats them as
"unverifiable" since it can't distinguish my own spawned processes from
unrelated ones). This came up twice while verifying the CPU/GPU load
monitor's new "stay open until closed" behavior (see
[[gpu_speedup_investigation]]) — each time I had to stop and ask. The
user said "i grand you permission to do so evry time" after the second
ask.

**How to apply:** Only for processes/windows I can concretely trace back
to a script or build I ran myself this session (e.g. a test script's
python.exe, or a femm.exe/fkn.exe launched by one of my own test
commands). Still don't touch unrelated PIDs or windows I can't attribute
to my own actions. This is scoped to the femmx project's
testing/dev workflow, not a blanket license elsewhere.
