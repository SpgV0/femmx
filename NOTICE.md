# Notice of Modifications

This repository, `femm_mods` (https://github.com/spgryparis/femm_mods), is a
derivative of Finite Element Method Magnetics (FEMM), originally distributed
by David C. Meeker at https://github.com/cenit/FEMM under the Aladdin Free
Public License v8 (see [license.txt](license.txt)). This repository
distributes the Program subject to the same license, at no charge, with
complete corresponding source code.

Per License section 2(c)(i), this file records repository-level
modifications and their dates. Any source file that is actually edited must
additionally carry a per-file notice (author, contact, date, purpose of the
change) at the point of modification.

## Change Log

See [README.md](README.md) for full technical detail on each change (dated
entries, most recent first). This section is the condensed, license-required
record of modifications.

- 2026-07-06: Repository cloned from https://github.com/cenit/FEMM
  (commit 7d9e8ed) and re-hosted at https://github.com/spgryparis/femm_mods.
  No source files were altered as part of this change.
- 2026-07-07: Added `femm/femmeLua.cpp` and `femm/FemmeDoc.h` changes
  (new `mi_setredraw` Lua command) and `femm/FemmeView.cpp` changes
  (`DrawPSLG()` and Copy/Move handlers now honor redraw suppression).
  See per-file modification notices for author/contact/date/purpose.
