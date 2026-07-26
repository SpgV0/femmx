---
name: pyfemm-is-pip-installed
description: "pyfemm (the `femm` Python module used by test/*.py) comes from PyPI, not this repo's pyfemm/ directory -- that directory's own source is incomplete/missing"
metadata: 
  node_type: memory
  type: reference
  originSessionId: 846a52dc-e5cc-4b0f-9a4f-7b5debeae297
  modified: 2026-07-21T11:17:45.376Z
---

`test/gpu_solver_test.py`, `test/thermal_gpu_solver_test.py`, etc. all
`import femm` and drive the classic GUI via COM automation
(`femm.ActiveFEMM`). That `femm` module is the **pip-installed pyfemm
package** (`pip install pyfemm pywin32`, per each test file's own
docstring) -- confirmed 2026-07-21 via
`python -c "import femm; print(femm.__file__)"` resolving to
`...\Python314\site-packages\femm\__init__.py`, a real, complete module
with full `hi_*`/`ho_*`/`mi_*`/etc. coverage.

**Why this matters:** this repo's own `pyfemm/` directory has
incomplete/missing source (surfaced during an earlier Lua-command-
coverage audit this session, where the user explicitly said to skip
auditing `pyfemm/` for that reason). Don't confuse the two -- the
in-repo `pyfemm/` directory is not what any test actually imports or
runs against.

**How to apply:** when writing or debugging a `test/*.py` script that
uses `femm.*` functions, check the actual installed package's real
signatures (`python -c "import inspect, femm; print(inspect.signature(femm.X))"`)
rather than assuming they match this repo's `pyfemm/` source, and don't
bother trying to keep this repo's `pyfemm/` directory in sync with new
Lua commands unless the user asks specifically about it.
