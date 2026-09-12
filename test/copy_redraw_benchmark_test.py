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

The absolute timings are trend data, not a pass/fail SLA: they go into a
human-readable report and a machine-readable JSON record (uploaded as a CI
artifact), where a gradual regression can actually be seen. CI runners are
too noisy for a threshold to mean anything at that resolution.

One wide guard-rail IS asserted, and only one: the suppressed run must not
cost multiples of the baseline, which would mean mi_setredraw had become
pathological rather than merely unhelpful. See MAX_ACCEPTABLE_SLOWDOWN for
why it is as wide as it is.

Requirements: pip install pyfemm pywin32; a built + COM-registered femmx.exe.

Usage:
    pytest copy_redraw_benchmark_test.py -v
    python copy_redraw_benchmark_test.py
"""

import datetime
import json
import os
import platform
import time

import pytest

import femm

CLUTTER_GRID = 40  # CLUTTER_GRID x CLUTTER_GRID small block labels
CLUTTER_SPACING_MM = 2.0
N_COPIES = 30  # number of separate mi_copytranslate calls per run
COPY_STEP_MM = 0.05  # translation per copy, kept tiny so copies stay on-screen

# Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-12
# (issue #22): widened from 1.5x, and each side is now timed several times
# with the best run taken.
#
# 1.5x was a performance SLA wearing a guard-rail's clothing. A shared CI
# runner can lose a whole scheduling quantum to a neighbour mid-loop, and
# the two runs here are seconds long, so a 50% excursion needs no defect at
# all -- which would have made this the flakiest test in the suite the
# moment CI started running on every push.
#
# What the assertion is actually for is catching mi_setredraw becoming
# pathological: suppressing redraw and then repainting once should never
# cost multiples of repainting every time. 4x cannot be reached by runner
# noise on a best-of-N measurement, and any real breakage of that kind is
# far larger than 4x.
#
# The numbers themselves are recorded as trend data (a JSON artifact, see
# TREND_PATH) rather than asserted on. That is where a gradual regression
# would actually show up; a threshold here could only ever catch a cliff.
MAX_ACCEPTABLE_SLOWDOWN = 4.0

# Each side is timed this many times and the BEST (minimum) is kept.
# Timing noise is one-sided -- interference can only ever make a run
# slower, never faster -- so the minimum is the sample least contaminated
# by whatever else the machine was doing, and the mean is the most.
TIMING_TRIALS = 3

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
RESULTS_DIR = os.path.join(SCRIPT_DIR, "results", "copy_redraw_benchmark")
RESULTS_PATH = os.path.join(RESULTS_DIR, "copy_benchmark.txt")
# Machine-readable trend record, uploaded as a CI artifact (#22).
TREND_PATH = os.path.join(RESULTS_DIR, "copy_benchmark_trend.json")


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
        # Interleaved rather than all-of-A-then-all-of-B, so a machine that
        # gets busier (or quieter) partway through biases both sides
        # equally instead of handing one of them a better environment.
        baselines, suppresseds = [], []
        for _ in range(TIMING_TRIALS):
            baselines.append(time_copy_run(suppress_redraw=False))
            suppresseds.append(time_copy_run(suppress_redraw=True))
    finally:
        femm.closefemm()

    t_baseline = min(baselines)
    t_suppressed = min(suppresseds)
    speedup = t_baseline / t_suppressed if t_suppressed > 0 else float("inf")

    def spread(samples):
        return (max(samples) - min(samples)) / min(samples) if min(samples) else 0.0

    lines = [
        "FEMM copy-function redraw benchmark",
        "====================================",
        f"Base model size:           {n_features} block labels",
        f"Copy actions per run:      {N_COPIES} (separate mi_copytranslate calls)",
        f"Trials per side:           {TIMING_TRIALS} (best of, interleaved)",
        "",
        f"A) Baseline (redraw after every copy):      {t_baseline:.3f} s  "
        f"({t_baseline / N_COPIES * 1000:.1f} ms/copy)",
        f"B) mi_setredraw(0)/(1) around the batch:    {t_suppressed:.3f} s  "
        f"({t_suppressed / N_COPIES * 1000:.1f} ms/copy)",
        "",
        f"Speedup: {speedup:.2f}x",
        "",
        "All trials (s):",
        f"  baseline:   {', '.join('%.3f' % t for t in baselines)}"
        f"   (spread {spread(baselines) * 100:.0f}%)",
        f"  suppressed: {', '.join('%.3f' % t for t in suppresseds)}"
        f"   (spread {spread(suppresseds) * 100:.0f}%)",
        "",
        "The timings above are trend data, not a pass/fail SLA -- see the",
        "comment on MAX_ACCEPTABLE_SLOWDOWN. The machine-readable copy is",
        f"{os.path.basename(TREND_PATH)}, uploaded as a CI artifact.",
    ]
    report = "\n".join(lines)

    with open(RESULTS_PATH, "w") as f:
        f.write(report + "\n")

    # Machine-readable, one record per run, so a series of CI artifacts can
    # be plotted or diffed. Everything a later comparison needs to know
    # whether two records are comparable at all is in here: a different
    # runner or a different model size makes the seconds meaningless.
    trend = {
        "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "machine": platform.node(),
        "platform": platform.platform(),
        "cpu_count": os.cpu_count(),
        "ci": bool(os.environ.get("CI")),
        "runner": os.environ.get("RUNNER_NAME", ""),
        "git_sha": os.environ.get("GITHUB_SHA", ""),
        "clutter_labels": n_features,
        "copies_per_run": N_COPIES,
        "trials": TIMING_TRIALS,
        "baseline_s": baselines,
        "suppressed_s": suppresseds,
        "baseline_best_s": t_baseline,
        "suppressed_best_s": t_suppressed,
        "speedup": speedup,
    }
    with open(TREND_PATH, "w", encoding="utf-8") as f:
        json.dump(trend, f, indent=2)

    return {"baseline": t_baseline, "suppressed": t_suppressed,
            "speedup": speedup, "baselines": baselines,
            "suppresseds": suppresseds}


def test_benchmark_report_was_written(benchmark_result):
    assert os.path.exists(RESULTS_PATH)
    assert os.path.getsize(RESULTS_PATH) > 0


def test_trend_record_is_written_and_complete(benchmark_result):
    """The artifact is the deliverable, so it has to be usable.

    A trend record missing the fields that say which machine and which
    model size produced it is not trend data, it is a number: two records
    from different runners are not comparable and nothing downstream could
    tell.
    """
    assert os.path.exists(TREND_PATH), "no trend record was written"
    with open(TREND_PATH, "r", encoding="utf-8") as fh:
        rec = json.load(fh)
    required = ("timestamp", "platform", "cpu_count", "clutter_labels",
                "copies_per_run", "trials", "baseline_s", "suppressed_s",
                "baseline_best_s", "suppressed_best_s", "speedup")
    missing = [k for k in required if k not in rec]
    assert not missing, "the trend record is missing %r" % missing
    assert len(rec["baseline_s"]) == TIMING_TRIALS
    assert len(rec["suppressed_s"]) == TIMING_TRIALS
    assert rec["baseline_best_s"] == min(rec["baseline_s"])


def test_suppressed_redraw_is_not_pathologically_slower(benchmark_result):
    """The one guard-rail: see MAX_ACCEPTABLE_SLOWDOWN for the width.

    Not a speedup assertion. Whether suppressing redraw is *faster*
    depends on the machine, the window state and whether the session is
    even visible, and asserting on that would be asserting on the runner.
    """
    baseline = benchmark_result["baseline"]
    suppressed = benchmark_result["suppressed"]
    assert suppressed <= baseline * MAX_ACCEPTABLE_SLOWDOWN, (
        f"mi_setredraw-suppressed copy loop ({suppressed:.3f}s, best of "
        f"{TIMING_TRIALS}: "
        f"{', '.join('%.3f' % t for t in benchmark_result['suppresseds'])}) "
        f"is more than {MAX_ACCEPTABLE_SLOWDOWN}x slower than the "
        f"unsuppressed baseline ({baseline:.3f}s, best of "
        f"{', '.join('%.3f' % t for t in benchmark_result['baselines'])}). "
        f"That is far outside timing noise on a best-of-N measurement, so "
        f"mi_setredraw is doing real work it should not be. See "
        f"{RESULTS_PATH}"
    )


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__, "-v"]))
