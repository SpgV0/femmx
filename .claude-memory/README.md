This folder mirrors Claude Code's persistent memory for this project
(normally at `~/.claude/projects/<project-hash>/memory/` on the machine
where Claude Code runs), committed here so it can be restored on another
machine.

## Restoring on another PC

Copy these `.md` files (except this `README.md`) into the local memory
directory for this project, e.g.:

```
cp .claude-memory/*.md ~/.claude/projects/<project-hash>/memory/
```

The `<project-hash>` directory name is derived from the absolute path
Claude Code is run from, so it differs per machine/clone location --
check `~/.claude/projects/` for the matching folder (or let Claude Code
create it on first run, then copy these files in).

Keep this folder in sync manually: after Claude Code writes new memory
files locally, copy them back here and commit.
