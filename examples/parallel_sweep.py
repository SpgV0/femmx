"""
Running several FEMMX instances at once to speed up a parameter sweep.

FEMMX supports this already -- no flag to set and nothing to configure.
Each ``femm.openfemm()`` starts its own ``femmx.exe``, and the instances
do not share state. What the caller must get right is one thing only:
give every worker its own scratch filename. See WHY IT WORKS below.

Run it:

    python parallel_sweep.py            # sweep, using all physical cores
    python parallel_sweep.py 4          # sweep, 4 workers
    python parallel_sweep.py compare    # sequential vs parallel, timed

---------------------------------------------------------------------
WHY IT WORKS, AND THE ONE RULE
---------------------------------------------------------------------

FEMMX's automation server is registered per-process, so two
``Dispatch('femm.ActiveFEMM')`` calls give two separate ``femmx.exe``
processes rather than two handles onto one. Nothing in the solve path
takes a global lock.

The solve pipeline names its intermediates after the DOCUMENT path --
``foo.fem`` produces ``foo.poly``, ``foo.pbc`` and ``foo.ans``. So the
one way to corrupt a parallel sweep is to have two workers save to the
same filename; they will then overwrite each other's mesh and solution
with no error and no warning, and the results will be quietly wrong.

    THE RULE: every worker saves to its own basename.

``worker_path()`` below does that using the process id. Everything else
is ordinary sweep code.

---------------------------------------------------------------------
CHOOSING THE WORKER COUNT
---------------------------------------------------------------------

The solvers are single-threaded -- one solve saturates one core -- so
workers scale with PHYSICAL cores, not logical ones. Hyperthreads add
little here because the PCG inner loops are memory-bound.

Starting and stopping an instance costs about 0.1 s, which is worth
avoiding but rarely decisive; for sweep points taking longer than a
second or so it is noise. This example still reuses one instance per
worker across all of that worker's points, since it costs nothing to do.

Memory is the real limit on large models: N workers hold N meshes at
once. If a single solve peaks at several GB, use fewer workers than you
have cores.

More workers is NOT automatically better, and the ``compare`` mode on
this deliberately small model shows why:

    sequential      3.8 s
    parallel x2     2.3 s   1.68x
    parallel x4     1.7 s   2.21x
    parallel x10    1.9 s   1.98x   <- slower than x4

Past a point there are more workers than there is work, and each one
still pays to start a process and an instance. Scale the worker count
to the job: for a sweep of a few short points, two or four beats ten.
The advantage grows with the cost of a single solve, so a sweep over
minute-long models scales far better than this example does.

What does NOT change is the answer -- ``compare`` reports the deviation
from the sequential run as exactly zero at every worker count, because
the instances are genuinely independent.
"""

import os
import sys
import time
from concurrent.futures import ProcessPoolExecutor

import femm

# The sweep: solve this wire-in-a-box at each frequency and report the
# terminal impedance. Substitute your own model and parameter.
FREQUENCIES = [50.0, 200.0, 1e3, 5e3, 20e3, 100e3, 500e3, 1e6]

SCRATCH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       'sweep_work')


def worker_path():
    """A scratch basename unique to this process.

    This is the one thing a parallel sweep must not get wrong: two
    workers sharing a basename will overwrite each other's .poly/.ans
    silently.
    """
    os.makedirs(SCRATCH, exist_ok=True)
    return os.path.join(SCRATCH, 'sweep_%d' % os.getpid()).replace(os.sep, '/')


def build_model(freq):
    """A current-carrying conductor inside a box with A = 0 on it."""
    femm.newdocument(0)
    femm.mi_probdef(freq, 'millimeters', 'planar', 1e-8, 100, 30)

    # Both endpoints must exist before a segment can join them, so add
    # every corner first and only then close the loops.
    def rect(a):
        for x, y in a:
            femm.mi_addnode(x, y)
        for i in range(len(a)):
            (x0, y0), (x1, y1) = a[i], a[(i + 1) % len(a)]
            femm.mi_addsegment(x0, y0, x1, y1)

    rect([(-40, -40), (40, -40), (40, 40), (-40, 40)])     # outer boundary
    rect([(-5, -5), (5, -5), (5, 5), (-5, 5)])             # conductor

    femm.mi_addmaterial('Copper', 1, 1, 0, 0, 58, 0, 0, 1, 0, 0, 0)
    femm.mi_addmaterial('Air', 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0)
    femm.mi_addcircprop('I', 1.0, 1)

    femm.mi_addboundprop('A0', 0, 0, 0, 0, 0, 0, 0, 0, 0)
    for x, y in ((0, -40), (0, 40), (-40, 0), (40, 0)):
        femm.mi_selectsegment(x, y)
    femm.mi_setsegmentprop('A0', 0, 1, 0, 0)
    femm.mi_clearselected()

    femm.mi_addblocklabel(0, 0)
    femm.mi_selectlabel(0, 0)
    femm.mi_setblockprop('Copper', 1, 0, 'I', 0, 0, 1)
    femm.mi_clearselected()

    femm.mi_addblocklabel(30, 30)
    femm.mi_selectlabel(30, 30)
    femm.mi_setblockprop('Air', 1, 0, '<None>', 0, 0, 0)
    femm.mi_clearselected()


def solve_chunk(freqs):
    """Solve several sweep points in ONE instance owned by this worker.

    Opening the instance once per chunk rather than once per point
    saves the ~0.1 s launch cost on every point after the first.
    """
    base = worker_path()
    out = []
    femm.openfemm(1)                    # 1 = hidden; no window to fight over
    try:
        for freq in freqs:
            build_model(freq)
            femm.mi_saveas(base + '.fem')
            femm.mi_analyze(1)          # 1 = no visible solver window
            femm.mi_loadsolution()
            V = complex(femm.mo_getcircuitproperties('I')[1])
            out.append((freq, abs(V)))
            # No mi_close()/mo_close() here: newdocument() replaces the
            # active document, and closing it first leaves the instance
            # with nothing selected, which fails the next mi_analyze().
    finally:
        femm.closefemm()
    return out


def chunk(seq, n):
    """Deal seq round-robin into n lists, so slow points spread evenly."""
    out = [[] for _ in range(n)]
    for i, item in enumerate(seq):
        out[i % n].append(item)
    return [c for c in out if c]


def run_parallel(freqs, workers):
    t0 = time.time()
    with ProcessPoolExecutor(max_workers=workers) as ex:
        results = [r for part in ex.map(solve_chunk, chunk(freqs, workers))
                   for r in part]
    return sorted(results), time.time() - t0


def physical_cores():
    try:
        import psutil
        return psutil.cpu_count(logical=False) or os.cpu_count()
    except ImportError:
        return max(1, (os.cpu_count() or 2) // 2)


def main():
    arg = sys.argv[1] if len(sys.argv) > 1 else None

    if arg == 'compare':
        seq, t_seq = run_parallel(FREQUENCIES, 1)
        print('sequential      : %5.1f s' % t_seq)
        for w in (2, 4, physical_cores()):
            par, t_par = run_parallel(FREQUENCIES, w)
            worst = max(abs(a[1] - b[1]) for a, b in zip(seq, par))
            print('parallel x%-2d    : %5.1f s   speedup %4.2fx   '
                  'max deviation from sequential %.2e'
                  % (w, t_par, t_seq / t_par, worst))
        print()
        print('Deviations should be exactly zero: the instances are')
        print('independent, so parallelism changes nothing numerically.')
        return

    workers = int(arg) if arg else physical_cores()
    print('sweeping %d points across %d workers' % (len(FREQUENCIES), workers))
    results, elapsed = run_parallel(FREQUENCIES, workers)

    print()
    print('%12s  %14s' % ('freq (Hz)', '|V| (V)'))
    for freq, v in results:
        print('%12.0f  %14.6e' % (freq, v))
    print()
    print('%d points in %.1f s' % (len(results), elapsed))


if __name__ == '__main__':
    main()
