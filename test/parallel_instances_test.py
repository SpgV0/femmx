"""
Regression tests for running several FEMMX instances concurrently.

FEMMX supports concurrent instances -- the automation server is
registered per-process, so each femm.openfemm() gets its own femmx.exe
and nothing in the solve path takes a global lock. examples/
parallel_sweep.py documents the pattern; these tests lock in the two
properties that make it safe to rely on:

  1. Two automation objects really are two processes, not two handles
     onto one. If this ever regressed to a shared server (the MFC
     REGCLS_MULTIPLEUSE behaviour), parallel sweeps would silently drive
     a single instance and trample each other's documents.

  2. Solving concurrently gives bit-identical answers to solving
     sequentially. The instances share nothing, so parallelism must not
     perturb a single number.

The second test doubles as a determinism check on the solver itself:
identical input must give identical output run to run, which is also
what guards changes to the linear-solve kernels (see fkn/spars.cpp).
"""

import os
import subprocess
from concurrent.futures import ProcessPoolExecutor

import pytest

import femm

RESULTS = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "results", "parallel_instances_test")

# A frequency sweep is a natural parallel workload: the points are
# independent, so any interference between instances shows up as a
# changed answer rather than as a crash.
FREQUENCIES = [0.0, 1e3, 50e3, 250e3]


def _femmx_pids():
    out = subprocess.run(
        ["tasklist", "/FI", "IMAGENAME eq femmx.exe", "/FO", "CSV", "/NH"],
        capture_output=True, text=True).stdout
    return {ln.split('","')[1] for ln in out.splitlines() if ln.startswith('"')}


def _build_and_solve(args):
    """Solve one sweep point in this process's own FEMMX instance.

    Module level so it is picklable for ProcessPoolExecutor.
    """
    index, freq = args
    os.makedirs(RESULTS, exist_ok=True)
    # Each worker MUST use its own basename: the solve pipeline names
    # .poly/.pbc/.ans after the document, so a shared name means workers
    # silently overwrite each other's mesh and solution.
    stem = os.path.join(RESULTS, "point_%d_%d" % (index, os.getpid()))

    femm.openfemm(1)
    try:
        femm.newdocument(0)
        femm.mi_probdef(freq, "millimeters", "planar", 1e-8, 100, 30)

        def rect(pts):
            for x, y in pts:
                femm.mi_addnode(x, y)
            for i in range(len(pts)):
                (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % len(pts)]
                femm.mi_addsegment(x0, y0, x1, y1)

        rect([(-40, -40), (40, -40), (40, 40), (-40, 40)])
        rect([(-8, -8), (8, -8), (8, 8), (-8, 8)])

        femm.mi_addmaterial("Copper", 1, 1, 0, 0, 58, 0, 0, 1, 0, 0, 0)
        femm.mi_addmaterial("Air", 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0)
        femm.mi_addcircprop("I", 1.0, 1)
        femm.mi_addboundprop("A0", 0, 0, 0, 0, 0, 0, 0, 0, 0)

        for x, y in ((0, -40), (0, 40), (-40, 0), (40, 0)):
            femm.mi_selectsegment(x, y)
        femm.mi_setsegmentprop("A0", 0, 1, 0, 0)
        femm.mi_clearselected()

        femm.mi_addblocklabel(0, 0)
        femm.mi_selectlabel(0, 0)
        femm.mi_setblockprop("Copper", 1, 0, "I", 0, 0, 1)
        femm.mi_clearselected()

        femm.mi_addblocklabel(30, 30)
        femm.mi_selectlabel(30, 30)
        femm.mi_setblockprop("Air", 1, 0, "<None>", 0, 0, 0)
        femm.mi_clearselected()

        femm.mi_saveas(stem.replace(os.sep, "/") + ".fem")
        femm.mi_analyze(1)
        femm.mi_loadsolution()
        V = complex(femm.mo_getcircuitproperties("I")[1])
        return index, freq, V.real, V.imag
    finally:
        femm.closefemm()


def test_two_dispatches_are_two_processes():
    """Each automation object must own a distinct femmx.exe.

    If this fails, the server has become a shared single instance and
    every parallel sweep is quietly driving one FEMM.
    """
    import win32com.client

    before = _femmx_pids()
    a = win32com.client.Dispatch("femm.ActiveFEMM")
    b = win32com.client.Dispatch("femm.ActiveFEMM")
    try:
        spawned = _femmx_pids() - before
        assert len(spawned) >= 2, (
            "expected two separate femmx.exe processes for two Dispatch "
            "calls, saw %d -- the automation server appears to be shared, "
            "which makes parallel sweeps unsafe" % len(spawned))
    finally:
        del a, b
        for pid in sorted(_femmx_pids() - before):
            subprocess.run(["taskkill", "/F", "/PID", pid],
                           capture_output=True)


def test_parallel_sweep_matches_sequential():
    """Concurrent solves must give exactly the sequential answers."""
    work = list(enumerate(FREQUENCIES))

    sequential = sorted(_build_and_solve(w) for w in work)

    with ProcessPoolExecutor(max_workers=len(work)) as pool:
        parallel = sorted(pool.map(_build_and_solve, work))

    assert len(parallel) == len(sequential)
    for (i, f, re_s, im_s), (j, g, re_p, im_p) in zip(sequential, parallel):
        assert (i, f) == (j, g)
        # Exact equality, not approximate: the instances share nothing,
        # so parallelism cannot legitimately change a single bit.
        assert re_p == re_s, (
            "point %d (%.0f Hz): Re(V) differs between the parallel and "
            "sequential runs (%r vs %r)" % (i, f, re_p, re_s))
        assert im_p == im_s, (
            "point %d (%.0f Hz): Im(V) differs between the parallel and "
            "sequential runs (%r vs %r)" % (i, f, im_p, im_s))


def test_solver_is_deterministic():
    """The same model solved twice must give the same answer.

    Guards the linear-solve kernels in fkn/spars.cpp: a change there that
    perturbs results -- reordering an accumulation, say -- shows up here
    even when it converges to something plausible.
    """
    first = _build_and_solve((900, 50e3))
    second = _build_and_solve((901, 50e3))
    assert first[2] == second[2] and first[3] == second[3], (
        "repeated solves of an identical model returned different results: "
        "%r then %r" % (first[2:], second[2:]))
