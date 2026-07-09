"""
Benchmark + regression check: repeated "copy" actions on a densely-drawn
model, with vs. without canvas redraw suppressed via the custom
mi_setredraw Lua command.

Background: FEMM's magnetics editor redraws the *entire* drawing (every
node/segment/arc/block label) on every single edit action, including each
individual mi_copytranslate/mi_copyrotate call. On a model that already has
many small features drawn, repeating a copy action N times therefore pays
for N full-canvas redraws. mi_setredraw(0)/mi_setredraw(1) (added in
femm/femmeLua.cpp) let a script suspend that redraw around a batch of edits
and force a single refresh at the end instead.

This module builds an identical "cluttered" base model twice (many small
block labels already on the canvas), then times a series of separate
mi_copytranslate calls against it:
  A) baseline:   redraw happens after every copy (default behavior)
  B) suppressed: mi_setredraw(0) .. copies .. mi_setredraw(1)

The absolute timings are informational (written to a report, not asserted
on -- CI runners are too noisy for a tight performance SLA), but a loose
regression guard is asserted: the suppressed run should not be dramatically
slower than the baseline, which would indicate mi_setredraw itself broke.

Requirements: pip install pyfemm pywin32; a built + COM-registered femmx.exe.

Usage:
    pytest copy_redraw_benchmark_test.py -v
    python copy_redraw_benchmark_test.py
"""

import os
import time

import pytest

import femm

CLUTTER_GRID = 40  # CLUTTER_GRID x CLUTTER_GRID small block labels
CLUTTER_SPACING_MM = 2.0
N_COPIES = 30  # number of separate mi_copytranslate calls per run
COPY_STEP_MM = 0.05  # translation per copy, kept tiny so copies stay on-screen

# CI runners are noisy; only flag the suppressed run as broken if it's
# dramatically slower than doing nothing at all, not just mildly slower.
MAX_ACCEPTABLE_SLOWDOWN = 1.5

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
RESULTS_DIR = os.path.join(SCRIPT_DIR, "results", "copy_redraw_benchmark")
RESULTS_PATH = os.path.join(RESULTS_DIR, "copy_benchmark.txt")


def build_cluttered_model():
    """Fresh magnetics document with a grid of small block labels already
    drawn, plus one extra label (outside the grid) used as the copy seed."""
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-8, 1, 30)
    femm.mi_getmaterial("Air")

    for i in range(CLUTTER_GRID):
        for j in range(CLUTTER_GRID):
            femm.mi_addblocklabel(i * CLUTTER_SPACING_MM, j * CLUTTER_SPACING_MM)

    seed_x = -5 * CLUTTER_SPACING_MM
    seed_y = -5 * CLUTTER_SPACING_MM
    femm.mi_addblocklabel(seed_x, seed_y)
    return seed_x, seed_y


def time_copy_run(suppress_redraw):
    seed_x, seed_y = build_cluttered_model()

    if suppress_redraw:
        femm.callfemm("mi_setredraw(0)")

    t0 = time.perf_counter()
    for k in range(1, N_COPIES + 1):
        femm.mi_selectlabel(seed_x, seed_y)
        femm.mi_copytranslate2(k * COPY_STEP_MM, 0, 1, 2)
        femm.mi_clearselected()
    if suppress_redraw:
        femm.callfemm("mi_setredraw(1)")
    elapsed = time.perf_counter() - t0

    return elapsed


@pytest.fixture(scope="module")
def benchmark_result():
    os.makedirs(RESULTS_DIR, exist_ok=True)
    femm.openfemm()
    try:
        n_features = CLUTTER_GRID * CLUTTER_GRID + 1
        t_baseline = time_copy_run(suppress_redraw=False)
        t_suppressed = time_copy_run(suppress_redraw=True)
    finally:
        femm.closefemm()

    speedup = t_baseline / t_suppressed if t_suppressed > 0 else float("inf")

    lines = [
        "FEMM copy-function redraw benchmark",
        "====================================",
        f"Base model size:           {n_features} block labels",
        f"Copy actions per run:      {N_COPIES} (separate mi_copytranslate calls)",
        "",
        f"A) Baseline (redraw after every copy):      {t_baseline:.3f} s  "
        f"({t_baseline / N_COPIES * 1000:.1f} ms/copy)",
        f"B) mi_setredraw(0)/(1) around the batch:    {t_suppressed:.3f} s  "
        f"({t_suppressed / N_COPIES * 1000:.1f} ms/copy)",
        "",
        f"Speedup: {speedup:.2f}x",
    ]
    report = "\n".join(lines)

    with open(RESULTS_PATH, "w") as f:
        f.write(report + "\n")

    return {"baseline": t_baseline, "suppressed": t_suppressed, "speedup": speedup}


def test_benchmark_report_was_written(benchmark_result):
    assert os.path.exists(RESULTS_PATH)
    assert os.path.getsize(RESULTS_PATH) > 0


def test_suppressed_redraw_not_slower_than_baseline(benchmark_result):
    baseline = benchmark_result["baseline"]
    suppressed = benchmark_result["suppressed"]
    assert suppressed <= baseline * MAX_ACCEPTABLE_SLOWDOWN, (
        f"mi_setredraw-suppressed copy loop ({suppressed:.3f}s) is more than "
        f"{MAX_ACCEPTABLE_SLOWDOWN}x slower than the unsuppressed baseline "
        f"({baseline:.3f}s) -- mi_setredraw may be broken. See {RESULTS_PATH}"
    )


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__, "-v"]))
