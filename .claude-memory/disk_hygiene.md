---
name: disk-hygiene
description: "Standing instruction to avoid leaving temporary/build/test garbage on disk over time, across all work in this project"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 846a52dc-e5cc-4b0f-9a4f-7b5debeae297
  modified: 2026-07-18T20:08:49.619Z
---

Don't let temporary files, build logs, test scratch files, or debug artifacts
accumulate on disk over the course of a session or across sessions.

**Why:** raised explicitly more than once — once mid-session ("delete
remnants of your previous work... so that I do not fill up my disc with
trash over time") leading to a real cleanup of ~117 scratchpad files (~51MB)
plus stray Temp-folder logs/debug files, and again as a standing instruction
("make sure you do not leave garbage in the disk overtime during your
work") while kicking off the new Qt-GUI effort — treat it as a durable
expectation, not a one-off request.

**How to apply:**
- Prefer the session scratchpad directory for anything temporary (test
  scripts, debug logs, screenshots); avoid scattering files into `%TEMP%`
  directly when the scratchpad works instead.
- When something must go in `%TEMP%` (e.g. a debug log a running femmx.exe
  process writes to, which can't target the scratchpad), delete it once
  it's served its purpose in the same working session, not "eventually."
- Delete temporary/debug instrumentation code from source files once it's
  answered its question (see the debug-tracing-then-strip pattern already
  used for `DbgLog`-style additions), not just the log files it produced.
- For new large-scope work (e.g. the Qt GUI + `.ansx` effort), keep build
  outputs inside the project's own `build*/`/`bin/` directories rather than
  ad hoc locations, and clean up any one-off conversion/test files created
  purely to verify a step once that verification is done.
- Don't wait to be asked a second time in a given area of work — clean up
  proactively as part of finishing a task, the way [[close_test_windows_permission]]
  already covers proactively closing spawned windows/processes.
