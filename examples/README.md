# Examples

Runnable scripts showing how to drive FEMMX from Python. Unlike `test/`,
nothing here asserts anything or runs in CI — these are worked examples
meant to be read and adapted.

Prerequisites are the same as for the tests: a built `femmx.exe`
registered as a COM automation server (`femm.ActiveFEMM`), plus
`pip install pyfemm pywin32`.

| Script | What it shows |
| --- | --- |
| `parallel_sweep.py` | Running several FEMMX instances at once to speed up a parameter sweep |

## `parallel_sweep.py`

FEMMX already supports concurrent instances — there is no flag to set.
Each `femm.openfemm()` starts its own `femmx.exe`, the automation server
is registered per-process, and nothing in the solve path takes a global
lock.

There is exactly one rule, and the script exists mainly to state it:

> **Every worker must save to its own basename.**

The solve pipeline names its intermediates after the document path, so
`foo.fem` produces `foo.poly`, `foo.pbc` and `foo.ans`. Two workers
sharing a basename overwrite each other's mesh and solution with no
error and no warning — the run completes and the numbers are quietly
wrong. This is the only way to get parallel sweeps wrong, and it fails
silently, which is why it is worth a worked example.

```
python parallel_sweep.py            # sweep across all physical cores
python parallel_sweep.py 4          # sweep with 4 workers
python parallel_sweep.py compare    # sequential vs parallel, timed
```

`compare` also verifies the point that matters: the deviation from the
sequential result is exactly zero at every worker count, because the
instances are genuinely independent.

Two practical notes, both measured rather than assumed:

- **Workers scale with physical cores, not logical ones.** The solvers
  are single-threaded, so one solve saturates one core; hyperthreads add
  little because the PCG inner loops are memory-bound.
- **More workers is not automatically better.** On a small model, ten
  workers came out *slower* than four — past a point there is less work
  than there are workers, and each still pays to start. The benefit
  grows with the cost of a single solve.

Memory is the binding constraint on large models: N workers hold N
meshes at once.
