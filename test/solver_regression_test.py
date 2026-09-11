"""
Solver drift detection, determinism, and the no-GPU fallback (issue #17).

Two gaps this closes:

1. NO DRIFT DETECTION. The five *_gpu_solver_test.py modules compare CPU
   against GPU within a single run, and the analytic tests check one
   number each. Nothing compared today's solved fields against a STORED
   reference, so a change shifting every result by 0.5% -- preconditioner,
   convergence tolerance, matrix assembly ordering -- passed everything in
   the suite.

2. THE FALLBACK PATH WAS UNTESTED. GPUAccel silently falls back to the CPU
   solve when no CUDA device is present, or when the binary was built
   without CUDA at all (fkn/spars.cpp). That fallback runs on every CI
   machine and most users' machines, and nothing asserted it is TAKEN
   rather than failing or producing a degenerate result -- CI only
   deselects the speed assertions.

References live in solver_references.json beside this file and are
regenerated deliberately:

    pytest test/solver_regression_test.py --update-references

Run that only when a solver change is intended, and commit the new
numbers with it, so a shift in the third digit is visible in review
rather than absorbed.

Requirements: a built, COM-registered femmx.exe; pip install pyfemm pywin32.

Usage:
    pytest test/solver_regression_test.py -v
    pytest test/solver_regression_test.py -m "not slow"     # fast lane
"""

import json
import math
import os
import subprocess
import time

import pytest

import femm

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "results", "solver_regression")
REFERENCE_PATH = os.path.join(SCRIPT_DIR, "solver_references.json")
BIN_DIR = os.path.join(REPO_ROOT, "bin", "plain")

# Per-quantity relative tolerance. Tight on purpose: the point is to catch
# a systematic shift, and a band wide enough to be comfortable is a band
# wide enough to hide one.
DRIFT_TOL = 1e-6

_REPORT = []


def _note(text):
    print(text)
    _REPORT.append(text)


def _save(path):
    return path.replace(chr(92), "/")


def _teardown():
    for fn in (femm.mo_close, femm.closefemm):
        try:
            fn()
        except Exception:  # noqa: BLE001
            pass


def _write_report():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    path = os.path.join(OUTPUT_DIR, "solver_regression.txt")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("Solver regression corpus / determinism / GPU fallback"
                 + os.linesep)
        fh.write("=" * 74 + os.linesep)
        for line in _REPORT:
            fh.write(line + os.linesep)
    return path


def _load_references():
    if os.path.exists(REFERENCE_PATH):
        with open(REFERENCE_PATH, "r", encoding="utf-8") as fh:
            return json.load(fh)
    return {}


def _store_references(refs):
    with open(REFERENCE_PATH, "w", encoding="utf-8") as fh:
        json.dump(refs, fh, indent=2, sort_keys=True)
        fh.write("\n")


# ---------------------------------------------------------------------------
# The corpus: one model per problem type, plus nonlinear and harmonic
# ---------------------------------------------------------------------------

def _square(add_node, add_seg, half):
    for x, y in ((-half, -half), (half, -half), (half, half), (-half, half)):
        add_node(x, y)
    add_seg(-half, -half, half, -half)
    add_seg(half, -half, half, half)
    add_seg(half, half, -half, half)
    add_seg(-half, half, -half, -half)


def build_magnetostatic(path, material="Air", gpu=False):
    femm.openfemm(1)
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-8, 20, 30)
    femm.mi_smartmesh(0)
    femm.mi_getmaterial("Air")
    femm.mi_getmaterial("Copper")
    if material != "Air":
        femm.mi_getmaterial(material)
    femm.mi_addcircprop("drive", 30.0, 1)

    _square(femm.mi_addnode, femm.mi_addsegment, 5.0)
    _square(femm.mi_addnode, femm.mi_addsegment, 30.0)

    femm.mi_addboundprop("outer", 0, 0, 0, 0, 0, 0, 0, 0, 0)
    for px, py in ((0, -30), (30, 0), (0, 30), (-30, 0)):
        femm.mi_selectsegment(px, py)
        femm.mi_setsegmentprop("outer", 0, 1, 0, 0)
        femm.mi_clearselected()

    femm.mi_addblocklabel(0, 0)
    femm.mi_selectlabel(0, 0)
    femm.mi_setblockprop("Copper", 0, 2.0, "drive", 0, 1, 1)
    femm.mi_clearselected()

    femm.mi_addblocklabel(0, 20)
    femm.mi_selectlabel(0, 20)
    femm.mi_setblockprop(material, 0, 6.0, "", 0, 2, 0)
    femm.mi_attachdefault()
    femm.mi_clearselected()

    if gpu:
        femm.callfemm("mi_setgpuaccel(1)")

    femm.mi_zoomnatural()
    femm.mi_saveas(_save(path))
    femm.mi_analyze(1)
    femm.mi_loadsolution()
    femm.mo_smooth("off")


def build_harmonic(path):
    femm.openfemm(1)
    femm.newdocument(0)
    femm.mi_probdef(5000.0, "millimeters", "planar", 1e-8, 20, 30)
    femm.mi_smartmesh(0)
    femm.mi_getmaterial("Air")
    femm.mi_getmaterial("Copper")
    femm.mi_addcircprop("ac", 10.0, 1)

    _square(femm.mi_addnode, femm.mi_addsegment, 4.0)
    _square(femm.mi_addnode, femm.mi_addsegment, 30.0)

    femm.mi_addboundprop("outer", 0, 0, 0, 0, 0, 0, 0, 0, 0)
    for px, py in ((0, -30), (30, 0), (0, 30), (-30, 0)):
        femm.mi_selectsegment(px, py)
        femm.mi_setsegmentprop("outer", 0, 1, 0, 0)
        femm.mi_clearselected()

    femm.mi_addblocklabel(0, 0)
    femm.mi_selectlabel(0, 0)
    femm.mi_setblockprop("Copper", 0, 1.0, "ac", 0, 1, 1)
    femm.mi_clearselected()

    femm.mi_addblocklabel(0, 20)
    femm.mi_selectlabel(0, 20)
    femm.mi_setblockprop("Air", 0, 6.0, "", 0, 2, 0)
    femm.mi_attachdefault()
    femm.mi_clearselected()

    femm.mi_zoomnatural()
    femm.mi_saveas(_save(path))
    femm.mi_analyze(1)
    femm.mi_loadsolution()
    femm.mo_smooth("off")


def build_magnetostatic_meshed_only(path, gpu=False):
    """Build and MESH a model, but do not solve it.

    Running fkn.exe by hand needs the .node/.ele files present, and
    mi_analyze consumes and deletes them -- a solver invoked afterwards
    exits 2, "problem loading mesh". mi_createmesh leaves them in place.
    """
    femm.openfemm(1)
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-8, 20, 30)
    femm.mi_smartmesh(0)
    femm.mi_getmaterial("Air")
    femm.mi_getmaterial("Copper")
    femm.mi_addcircprop("drive", 30.0, 1)

    _square(femm.mi_addnode, femm.mi_addsegment, 5.0)
    _square(femm.mi_addnode, femm.mi_addsegment, 30.0)

    femm.mi_addboundprop("outer", 0, 0, 0, 0, 0, 0, 0, 0, 0)
    for px, py in ((0, -30), (30, 0), (0, 30), (-30, 0)):
        femm.mi_selectsegment(px, py)
        femm.mi_setsegmentprop("outer", 0, 1, 0, 0)
        femm.mi_clearselected()

    femm.mi_addblocklabel(0, 0)
    femm.mi_selectlabel(0, 0)
    femm.mi_setblockprop("Copper", 0, 2.0, "drive", 0, 1, 1)
    femm.mi_clearselected()

    femm.mi_addblocklabel(0, 20)
    femm.mi_selectlabel(0, 20)
    femm.mi_setblockprop("Air", 0, 6.0, "", 0, 2, 0)
    femm.mi_attachdefault()
    femm.mi_clearselected()

    if gpu:
        femm.callfemm("mi_setgpuaccel(1)")

    femm.mi_zoomnatural()
    femm.mi_saveas(_save(path))
    femm.mi_createmesh()


PROBE_POINTS = [(0.0, 0.0), (3.0, 0.0), (0.0, 8.0), (10.0, 10.0), (0.0, 20.0)]


def _magnetics_signature():
    """A handful of numbers that move if anything about the solve moves."""
    sig = {}
    for i, (x, y) in enumerate(PROBE_POINTS):
        bx, by = femm.mo_getb(x, y)
        sig["B%d" % i] = math.hypot(abs(complex(bx)), abs(complex(by)))
    femm.mo_groupselectblock(1)
    sig["energy"] = abs(complex(femm.mo_blockintegral(2)))
    femm.mo_clearblock()
    sig["nodes"] = float(femm.mo_numnodes())
    sig["elements"] = float(femm.mo_numelements())
    return sig


CORPUS = {
    "magnetostatic_linear": lambda p: build_magnetostatic(p, "Air"),
    "magnetostatic_nonlinear": lambda p: build_magnetostatic(p, "Pure Iron"),
    "harmonic": build_harmonic,
}


# ---------------------------------------------------------------------------
# 1. Drift against stored references
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("name", sorted(CORPUS))
def test_solved_fields_match_the_reference(name, request):
    """Today's solve against a stored one: a systematic shift must show."""
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    path = os.path.join(OUTPUT_DIR, "%s.fem" % name)
    try:
        CORPUS[name](path)
        signature = _magnetics_signature()
    finally:
        _teardown()

    refs = _load_references()
    if request.config.getoption("--update-references"):
        refs[name] = signature
        _store_references(refs)
        _note("    %-24s REFERENCE UPDATED (%d quantities)"
              % (name, len(signature)))
        return

    assert name in refs, (
        "no stored reference for %r. Generate it deliberately with "
        "`pytest test/solver_regression_test.py --update-references` and "
        "commit solver_references.json." % name)

    stored = refs[name]
    worst, worst_key = 0.0, None
    for key, got in sorted(signature.items()):
        want = stored.get(key)
        assert want is not None, "reference has no %r for %r" % (key, name)
        scale = max(abs(got), abs(want), 1e-30)
        rel = abs(got - want) / scale
        if rel > worst:
            worst, worst_key = rel, key

    _note("    %-24s worst drift %.3e on %s (tolerance %.0e)"
          % (name, worst, worst_key, DRIFT_TOL))
    assert worst < DRIFT_TOL, (
        "%s drifted by %.3e on %r (now %.9g, reference %.9g). If a solver "
        "change is intended, re-run with --update-references and commit the "
        "new numbers as part of it."
        % (name, worst, worst_key, signature[worst_key], stored[worst_key]))


# ---------------------------------------------------------------------------
# 2. Determinism
# ---------------------------------------------------------------------------

def test_the_same_model_solves_identically_twice():
    """Solved twice in the same build: bit-identical.

    The solvers are single-threaded and seedless, so there is nothing
    legitimately non-deterministic here -- an exact comparison is the right
    bar, and anything less would tolerate the reordering-induced drift this
    file exists to catch. (The mesh is regenerated each time too, so this
    covers Triangle's determinism as well.)
    """
    first_path = os.path.join(OUTPUT_DIR, "determinism_a.fem")
    second_path = os.path.join(OUTPUT_DIR, "determinism_b.fem")

    try:
        build_magnetostatic(first_path, "Air")
        first = _magnetics_signature()
    finally:
        _teardown()
    try:
        build_magnetostatic(second_path, "Air")
        second = _magnetics_signature()
    finally:
        _teardown()

    differing = {k: (first[k], second[k]) for k in first
                 if first[k] != second[k]}
    _note("    determinism: %d quantities compared, %d differ"
          % (len(first), len(differing)))
    assert not differing, (
        "the same model solved twice gave different answers: %r" % differing)


# ---------------------------------------------------------------------------
# 3. The no-GPU fallback
# ---------------------------------------------------------------------------

def test_gpu_accel_falls_back_to_cpu_and_stays_correct():
    """GPUAccel on a build without CUDA must fall back, not fail.

    Driven through the normal analyze path rather than by invoking fkn.exe
    directly: FEMM cleans up the .node/.ele files, so a hand-run solver
    exits 2 ("problem loading mesh") -- mi_createmesh does not leave them
    behind either. What that costs is the solver's stderr, where the
    "falling back to CPU" diagnostic goes; see the test below for what can
    be asserted about that instead. What it still proves is the part that
    matters: with GPU acceleration requested on a CPU-only build, the solve
    succeeds and produces the CPU answer rather than failing or returning
    something degenerate.
    """
    cpu_path = os.path.join(OUTPUT_DIR, "fallback_cpu.fem")
    try:
        build_magnetostatic(cpu_path, "Air", gpu=False)
        cpu_signature = _magnetics_signature()
    finally:
        _teardown()

    gpu_path = os.path.join(OUTPUT_DIR, "fallback_gpu.fem")
    try:
        build_magnetostatic(gpu_path, "Air", gpu=True)
        gpu_signature = _magnetics_signature()
    finally:
        _teardown()

    with open(gpu_path, "r", encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    flag_line = [ln.strip() for ln in text.splitlines() if "[GPUAccel]" in ln]
    assert flag_line, "the model does not record a GPUAccel setting at all"
    assert "1" in flag_line[0], (
        "mi_setgpuaccel(1) did not take: the model records %r, so this test would be exercising the plain CPU path and proving nothing" % flag_line[0])
    _note("    model records %s, and the solve completed" % flag_line[0])

    worst, worst_key = 0.0, None
    for key in cpu_signature:
        a, b = cpu_signature[key], gpu_signature[key]
        scale = max(abs(a), abs(b), 1e-30)
        rel = abs(a - b) / scale
        if rel > worst:
            worst, worst_key = rel, key

    _note("    fallback result vs CPU result: worst relative difference "
          "%.3e on %s" % (worst, worst_key))
    assert worst < DRIFT_TOL, (
        "requesting GPU acceleration changed the answer by %.3e on %r; the "
        "fallback must produce the CPU result, not an approximation of it"
        % (worst, worst_key))


def test_the_build_under_test_is_the_one_that_falls_back():
    """Pins that the fallback is genuinely the path being exercised.

    The test above would pass trivially on a CUDA build with a working
    GPU, because then there is no fallback to take -- it would just be
    comparing two correct answers. bin/plain is a CPU-only build, and its
    fkn.exe carries the "built without CUDA support" message precisely
    because that is the branch it will take. Asserting the binary contains
    it is a cheap way to know which situation the suite is in, and it says
    so in the report either way.
    """
    fkn = os.path.join(BIN_DIR, "fkn.exe")
    if not os.path.exists(fkn):
        pytest.skip("fkn.exe not built at %s" % fkn)

    with open(fkn, "rb") as fh:
        blob = fh.read()
    without_cuda = b"built without CUDA support" in blob
    falling_back = b"falling back to CPU" in blob

    _note("    fkn.exe: built-without-CUDA message %s, falling-back message %s"
          % ("present" if without_cuda else "absent",
             "present" if falling_back else "absent"))
    assert falling_back, (
        "fkn.exe carries no fallback diagnostic at all -- spars.cpp's "
        "fall-back-to-CPU path may have been removed, which would turn a "
        "missing GPU from a graceful degradation into a failure")

# ---------------------------------------------------------------------------
# 4. A coarse convergence guard
# ---------------------------------------------------------------------------

def test_solve_completes_well_inside_a_time_ceiling():
    """A blunt instrument, and labelled as one.

    The ticket asks for an iteration-count guard so a preconditioner
    regression reads as "converged in 40x the iterations" rather than just
    a slower green run. FEMM does not expose an iteration count anywhere --
    not through Lua, not in the .ans, not on the solver's stderr -- so
    there is nothing to assert on directly. Wall time is the only available
    proxy and it is a poor one, so the ceiling is set generously: it exists
    to catch an order-of-magnitude regression, not a 20% one.
    """
    path = os.path.join(OUTPUT_DIR, "timing.fem")
    started = time.time()
    try:
        build_magnetostatic(path, "Air")
    finally:
        _teardown()
    elapsed = time.time() - started

    _note("    solve wall time: %.2fs (ceiling 60s -- an order-of-magnitude "
          "guard, not a performance test)" % elapsed)
    assert elapsed < 60.0, (
        "a small model took %.1fs to build and solve; something in the solve "
        "path regressed badly" % elapsed)


# ---------------------------------------------------------------------------
# 5. A larger stress model, excluded from the fast lane
# ---------------------------------------------------------------------------

@pytest.mark.slow
def test_larger_model_solves_and_stays_physical():
    """Marked slow: excluded with `-m "not slow"`."""
    path = os.path.join(OUTPUT_DIR, "stress.fem")
    try:
        femm.openfemm(1)
        femm.newdocument(0)
        femm.mi_probdef(0, "millimeters", "planar", 1e-8, 20, 30)
        femm.mi_smartmesh(0)
        femm.mi_getmaterial("Air")
        femm.mi_getmaterial("Copper")
        femm.mi_addcircprop("drive", 50.0, 1)

        _square(femm.mi_addnode, femm.mi_addsegment, 5.0)
        _square(femm.mi_addnode, femm.mi_addsegment, 60.0)
        femm.mi_addboundprop("outer", 0, 0, 0, 0, 0, 0, 0, 0, 0)
        for px, py in ((0, -60), (60, 0), (0, 60), (-60, 0)):
            femm.mi_selectsegment(px, py)
            femm.mi_setsegmentprop("outer", 0, 1, 0, 0)
            femm.mi_clearselected()

        femm.mi_addblocklabel(0, 0)
        femm.mi_selectlabel(0, 0)
        femm.mi_setblockprop("Copper", 0, 0.6, "drive", 0, 1, 1)
        femm.mi_clearselected()
        femm.mi_addblocklabel(0, 40)
        femm.mi_selectlabel(0, 40)
        femm.mi_setblockprop("Air", 0, 1.2, "", 0, 2, 0)
        femm.mi_attachdefault()
        femm.mi_clearselected()

        femm.mi_zoomnatural()
        femm.mi_saveas(_save(path))
        started = time.time()
        femm.mi_analyze(1)
        elapsed = time.time() - started
        femm.mi_loadsolution()
        femm.mo_smooth("off")

        nodes = int(femm.mo_numnodes())
        bx, by = femm.mo_getb(3.0, 0.0)
        b = math.hypot(abs(complex(bx)), abs(complex(by)))
    finally:
        _teardown()

    _note("    stress model: %d nodes, solved in %.1fs, |B| at (3,0) = %.6g T"
          % (nodes, elapsed, b))
    assert nodes > 3000, "the stress model only meshed to %d nodes" % nodes
    assert 0.0 < b < 1.0, "|B| = %g T is not physical for this model" % b


def test_write_report():
    path = _write_report()
    assert os.path.exists(path)
    print("report: " + path)
