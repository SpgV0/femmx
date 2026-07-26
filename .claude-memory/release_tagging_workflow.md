---
name: release-tagging-workflow
description: "The step-by-step versioning/tag/release process for femmx, used 3x this session (v1.1.0, v1.1.1, v1.2.0) — what files to bump, in what order, and how to merge/push"
metadata: 
  node_type: memory
  type: project
  originSessionId: 846a52dc-e5cc-4b0f-9a4f-7b5debeae297
---

When the user says something like "version/tag/release as vX.Y.Z" or
"step the software", follow this exact sequence (established and
repeated across v1.1.0, v1.1.1, and v1.2.0):

1. **Bump version in exactly two places** (single source of truth split
   across a C file and an NSIS script, which can't share `#define`s):
   - `script.nsi`: `!define PROJECT_VERSION "X.Y.Z"`
   - `femm/femm.rc`: as of v1.2.0, 4 of the original 6 hardcoded spots
     are driven by macros near the top of the file
     (`FEMMX_VERSION_MAJOR/MINOR/PATCH/BUILD`, stringized via the
     standard `#x` two-macro idiom into `FEMMX_VERSION_STR_CSV`) — bump
     those 4 numbers once and `VERSIONINFO`'s `FILEVERSION`/
     `PRODUCTVERSION`/`FileVersion`/`ProductVersion` VALUE strings
     update automatically. `IDD_ABOUTBOX`'s `LTEXT` and
     `IDR_MAINFRAME`'s `STRINGTABLE` entry stay **hardcoded strings**,
     bumped by hand each time — confirmed by a real build failure
     (RC2116/RC2108) that classic dialog-item/string-table text does
     NOT support the same macro-expands-to-concatenated-string-literals
     trick that `VERSIONINFO`'s `VALUE` statement does. Don't retry
     macro-izing those two without expecting the same failure.
   - Add a dated `// Modified by Claude...` comment above each change,
     matching this repo's per-file-notice convention (also required by
     `NOTICE.md`'s license terms for any edited file).
2. **Update `CHANGELOG.md`**: new heading at the very top,
   `DDMonYYYY (vX.Y.Z)`, with bullet points summarizing every change
   since the last version heading (check `git log --oneline
   <last-tag-commit>..HEAD` to enumerate them — don't rely on memory of
   the conversation, some commits may have already been pushed earlier
   in the session without a changelog entry yet). This repo pairs every
   `CHANGELOG.md` heading with a version+tag; don't add an entry without
   one (see [[push_branch_policy]] for the general commit/push cadence
   this fits into).
3. **Rebuild via `build_plain.bat`** (not bare `build.ps1` — see
   [[build_and_com_registration_gotchas]]) to confirm the version bump
   compiles clean before committing. The installer filename
   (`FEMMX_v<version>_installer.exe`) is a quick visual confirmation the
   bump actually took.
4. **Commit** the version bump + CHANGELOG together on `new_features`.
5. **Tag**: `git tag -a vX.Y.Z -m "..."` (annotated, with a short bullet
   summary in the message body) at the tip of `new_features`.
6. **Merge `--no-ff` into both `rc` and `main`**, in that order:
   `git checkout rc && git merge --no-ff new_features -m "Merge
   new_features (vX.Y.Z) into rc"`, then the same for `main`. These have
   been clean fast merges every time so far (no conflicts) since `rc`/
   `main` only ever receive fully-formed release merges, never direct
   commits.
7. **Switch back to `new_features`** (`git checkout new_features`) —
   this repo's convention is to leave the working branch as
   `new_features` between sessions, never sitting on `rc`/`main`.
8. **Push everything**: `new_features`, `rc`, `main`, and the tag
   (4 separate `git push` invocations, or `git push origin
   new_features rc main vX.Y.Z`). Do NOT push to `master` — that branch
   was deleted from the remote (2026-07-17); `main` is now the default,
   `origin/HEAD` already points there.

This whole sequence only runs on an **explicit** request naming a
version/tag/release — per [[push_branch_policy]], regular bug-fix
commits during a session stay on `new_features` and get pushed there
alone, without a version bump, until the user asks for a release.
