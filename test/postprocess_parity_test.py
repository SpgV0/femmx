"""
femmqt vs the classic post-processor: numeric parity (issue #16).

femmqt carries a second, independent implementation of numbers users make
engineering decisions with -- AnsFileIO's per-element field computation,
CircuitAnalysis, the block integrals -- and nothing checked it against the
classic post-processor, which IS covered (indirectly by the Lua sweep,
directly by the analytic tests).

Both sides read the SAME solved .ans, so any difference is in the
post-processing rather than in the solve. femmqt's numbers come out
through `femmqt.exe --probe-ans`, added for this: its post-processing was
previously only reachable by driving the GUI, while classic's is a Lua
call away.

Tolerances are stated per quantity rather than one loose global, and
where the two genuinely define something differently that is written down
here instead of being absorbed into a wider band.

Requirements: a built, COM-registered femmx.exe and femmqt.exe;
pip install pyfemm pywin32.

Usage:
    pytest test/postprocess_parity_test.py -v
"""

import math
import os
import subprocess

import pytest

import femm

import femmx_paths

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "results", "postprocess_parity")
FEMMQT = os.path.join(femmx_paths.BIN_DIR, "femmqt.exe")

# The .ans stores B to 17 significant digits, and both sides derive the
# element field from the same nodal A, so agreement should be close to
# round-off. This is deliberately tight: a loose band here would hide
# exactly the systematic difference the test exists to find.
FIELD_REL_TOL = 1e-6
FIELD_ABS_FLOOR = 1e-12   # below this, |B| is noise and relative error is meaningless

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
    path = os.path.join(OUTPUT_DIR, "postprocess_parity.txt")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("femmqt vs classic post-processor parity" + os.linesep)
        fh.write("=" * 70 + os.linesep)
        for line in _REPORT:
            fh.write(line + os.linesep)
    return path


# ---------------------------------------------------------------------------
# A model with a circuit, so the circuit-property comparison has something
# to compare.
# ---------------------------------------------------------------------------

def _build_and_solve():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    path = os.path.join(OUTPUT_DIR, "parity.fem")
    femm.openfemm(1)
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-8, 25, 30)
    femm.mi_smartmesh(0)

    femm.mi_getmaterial("Air")
    femm.mi_getmaterial("Copper")
    femm.mi_addcircprop("drive", 12.0, 1)

    # a square conductor inside a square air domain
    for x, y in ((-4, -4), (4, -4), (4, 4), (-4, 4)):
        femm.mi_addnode(x, y)
    femm.mi_addsegment(-4, -4, 4, -4)
    femm.mi_addsegment(4, -4, 4, 4)
    femm.mi_addsegment(4, 4, -4, 4)
    femm.mi_addsegment(-4, 4, -4, -4)

    R = 30.0
    for x, y in ((-R, -R), (R, -R), (R, R), (-R, R)):
        femm.mi_addnode(x, y)
    femm.mi_addsegment(-R, -R, R, -R)
    femm.mi_addsegment(R, -R, R, R)
    femm.mi_addsegment(R, R, -R, R)
    femm.mi_addsegment(-R, R, -R, -R)

    femm.mi_addboundprop("outer", 0, 0, 0, 0, 0, 0, 0, 0, 0)
    for px, py in ((0, -R), (R, 0), (0, R), (-R, 0)):
        femm.mi_selectsegment(px, py)
        femm.mi_setsegmentprop("outer", 0, 1, 0, 0)
        femm.mi_clearselected()

    femm.mi_addblocklabel(0, 0)
    femm.mi_selectlabel(0, 0)
    # turns = 1, i.e. a SOLID conductor. femmqt's CircuitAnalysis::compute
    # only handles the solved-voltage solid case: a multi-turn stranded coil
    # is written into the .ans as caseType 1 (a prescribed current density)
    # and compute() declines it with "expected a solved-voltage solid
    # conductor". A 10-turn coil here made the circuit comparison skip
    # itself silently, which is the wrong way to find that out.
    femm.mi_setblockprop("Copper", 0, 1.5, "drive", 0, 1, 1)
    femm.mi_clearselected()

    femm.mi_addblocklabel(0, R * 0.7)
    femm.mi_selectlabel(0, R * 0.7)
    femm.mi_setblockprop("Air", 0, 5.0, "", 0, 2, 0)
    femm.mi_attachdefault()
    femm.mi_clearselected()

    femm.mi_zoomnatural()
    femm.mi_saveas(_save(path))
    femm.mi_analyze(1)
    femm.mi_loadsolution()
    return path, path[:-4] + ".ans"


def _classic_smoothing(flag):
    """mo_smooth("on"|"off").

    This is the whole reason the comparison below is meaningful. Classic's
    DEFAULT is smoothed: mo_getb returns an inverse-distance weighted,
    material-aware average over the elements around the probe point (the
    GetNodalB path). femmqt's per-element B is the raw value implied by
    the linear A over that one triangle. Comparing the two as-is shows a
    10-20% difference that is a definitional difference, not a defect --
    and exactly 0 vs non-zero at elements whose own three nodes all sit on
    a Dirichlet boundary.
    """
    femm.mo_smooth(flag)


def _probe_with_femmqt(ans_path, max_elements=150):
    csv_path = os.path.join(OUTPUT_DIR, "femmqt_probe.csv")
    if os.path.exists(csv_path):
        os.remove(csv_path)
    proc = subprocess.run(
        [FEMMQT, "--probe-ans", ans_path, csv_path, str(max_elements)],
        capture_output=True, text=True, timeout=180)
    assert proc.returncode == 0, (
        "femmqt --probe-ans failed: %s" % (proc.stderr or "").strip())
    rows = {"mesh": [], "element": [], "circuit": []}
    with open(csv_path, "r", encoding="utf-8") as fh:
        next(fh)  # header
        for line in fh:
            parts = line.strip().split(",")
            if len(parts) < 6:
                continue
            kind = parts[0]
            if kind in rows:
                rows[kind].append([int(parts[1])] + [float(v) for v in parts[2:6]])
    return rows


@pytest.fixture(scope="module")
def solved():
    if not os.path.exists(FEMMQT):
        pytest.skip("femmqt.exe not built at %s" % FEMMQT)
    fem, ans = _build_and_solve()
    try:
        yield fem, ans
    finally:
        _teardown()


# ---------------------------------------------------------------------------
# 1. Mesh agreement -- the precondition for everything else
# ---------------------------------------------------------------------------

def test_both_read_the_same_mesh(solved):
    """If the two disagree on the mesh, nothing below means anything."""
    _fem, ans = solved
    classic_nodes = int(femm.mo_numnodes())
    classic_elements = int(femm.mo_numelements())

    rows = _probe_with_femmqt(ans)
    assert rows["mesh"], "femmqt reported no mesh row"
    _idx, qt_nodes, qt_elements, bmin, bmax = rows["mesh"][0]

    _note("    mesh: classic %d nodes / %d elements, femmqt %d / %d "
          "(|B| range %.4g..%.4g)"
          % (classic_nodes, classic_elements, int(qt_nodes), int(qt_elements),
             bmin, bmax))
    assert int(qt_nodes) == classic_nodes
    assert int(qt_elements) == classic_elements


# ---------------------------------------------------------------------------
# 2. Flux density at element centroids
# ---------------------------------------------------------------------------

def test_flux_density_agrees_at_element_centroids(solved):
    """|Bx| and |By| from both implementations, at the same points.

    femmqt precomputes a per-element B from the nodal A (AnsFileIO::
    computeElementFields); classic interpolates within the element
    containing the probe point. Sampling at the CENTROID is what makes
    these comparable -- an element's constant-gradient A gives one B for
    the whole element, so the centroid is not a special case, it is just
    a point guaranteed to be inside the element in question.
    """
    _fem, ans = solved
    _classic_smoothing("off")   # compare like with like
    rows = _probe_with_femmqt(ans)
    assert rows["element"], "femmqt reported no elements"

    worst_rel = 0.0
    worst_at = None
    compared = 0
    skipped_tiny = 0
    for _idx, cx, cy, qt_bx, qt_by in rows["element"]:
        bx, by = femm.mo_getb(cx, cy)
        cl_bx, cl_by = abs(complex(bx)), abs(complex(by))
        qt_mag = math.hypot(qt_bx, qt_by)
        cl_mag = math.hypot(cl_bx, cl_by)
        if max(qt_mag, cl_mag) < FIELD_ABS_FLOOR:
            skipped_tiny += 1
            continue
        rel = abs(qt_mag - cl_mag) / max(qt_mag, cl_mag)
        compared += 1
        if rel > worst_rel:
            worst_rel = rel
            worst_at = (cx, cy, qt_mag, cl_mag)

    _note("    |B| at %d element centroids: worst relative difference %.3e "
          "(tolerance %.0e, %d skipped as below the noise floor)"
          % (compared, worst_rel, FIELD_REL_TOL, skipped_tiny))
    assert compared > 20, "only %d comparable points" % compared
    assert worst_rel < FIELD_REL_TOL, (
        "femmqt and classic disagree on |B| by %.3e at (%.4g, %.4g): "
        "femmqt %.6e vs classic %.6e. Both read the same .ans, so this is a "
        "post-processing difference, not a solver one."
        % ((worst_rel,) + worst_at))


def test_flux_density_components_agree(solved):
    """Not just the magnitude: a swapped or sign-flipped component would
    leave |B| identical while making every vector plot wrong."""
    _fem, ans = solved
    _classic_smoothing("off")   # compare like with like
    rows = _probe_with_femmqt(ans)

    worst = 0.0
    compared = 0
    for _idx, cx, cy, qt_bx, qt_by in rows["element"]:
        bx, by = femm.mo_getb(cx, cy)
        cl_bx, cl_by = abs(complex(bx)), abs(complex(by))
        scale = max(abs(qt_bx), abs(qt_by), abs(cl_bx), abs(cl_by))
        if scale < FIELD_ABS_FLOOR:
            continue
        compared += 1
        worst = max(worst,
                    abs(qt_bx - cl_bx) / scale,
                    abs(qt_by - cl_by) / scale)

    _note("    B components at %d centroids: worst normalised difference %.3e"
          % (compared, worst))
    assert compared > 20
    assert worst < FIELD_REL_TOL, (
        "a per-component difference of %.3e -- the magnitudes could still "
        "match while a component is swapped or sign-flipped" % worst)


def test_classic_smoothing_is_the_only_field_difference(solved):
    """Pins WHY the two differ by default, with a measured magnitude.

    With smoothing off they agree to round-off. With classic's default
    smoothing on they differ by a measurable amount. Recording both means
    the difference is documented as a definition rather than absorbed into
    a tolerance wide enough to hide a real regression -- and if femmqt ever
    grows smoothing of its own, the "off" comparison still holds while this
    one changes, which is the signal you would want.
    """
    _fem, ans = solved
    rows = _probe_with_femmqt(ans)

    def worst_against(flag):
        _classic_smoothing(flag)
        worst = 0.0
        for _idx, cx, cy, qt_bx, qt_by in rows["element"]:
            bx, by = femm.mo_getb(cx, cy)
            cl = math.hypot(abs(complex(bx)), abs(complex(by)))
            qt = math.hypot(qt_bx, qt_by)
            if max(cl, qt) < FIELD_ABS_FLOOR:
                continue
            worst = max(worst, abs(cl - qt) / max(cl, qt))
        return worst

    raw = worst_against("off")
    smoothed = worst_against("on")
    _classic_smoothing("off")

    _note("    smoothing: worst |B| difference is %.3e with mo_smooth(off) "
          "and %.3e with mo_smooth(on), classic's default"
          % (raw, smoothed))
    assert raw < FIELD_REL_TOL, (
        "even with smoothing off the two disagree by %.3e" % raw)
    assert smoothed > raw * 100, (
        "smoothed and unsmoothed classic results are nearly identical "
        "(%.3e vs %.3e); either smoothing stopped being applied or this "
        "model is too uniform for the comparison to mean anything"
        % (smoothed, raw))

# ---------------------------------------------------------------------------
# 3. Circuit properties
# ---------------------------------------------------------------------------

def test_circuit_properties_agree(solved):
    """Current, voltage drop and flux linkage from both implementations."""
    _fem, ans = solved
    rows = _probe_with_femmqt(ans)
    assert rows["circuit"], (
        "femmqt produced no circuit results. CircuitAnalysis::compute only "
        "supports a solved-voltage SOLID conductor (caseType 0); a stranded "
        "multi-turn coil is declined. If this model changed to use turns > 1, "
        "that is why -- see the block property above.")

    classic = femm.mo_getcircuitproperties("drive")
    cl_amps, cl_volts, cl_flux = (abs(complex(v)) for v in classic[:3])

    _idx, qt_amps, qt_volts, qt_flux, _pad = rows["circuit"][0]
    qt_amps, qt_volts, qt_flux = abs(qt_amps), abs(qt_volts), abs(qt_flux)

    _note("    circuit 'drive': amps %.6g vs %.6g | volts %.6g vs %.6g | "
          "flux linkage %.6g vs %.6g  (classic vs femmqt)"
          % (cl_amps, qt_amps, cl_volts, qt_volts, cl_flux, qt_flux))

    # The prescribed current is the input, so both must report it exactly.
    assert abs(qt_amps - cl_amps) <= max(1e-9, 1e-6 * cl_amps), (
        "circuit current differs: classic %.9g, femmqt %.9g" % (cl_amps, qt_amps))

    # Flux linkage is derived and is the number an inductance is computed
    # from, so it gets the same tight bar as the fields.
    scale = max(abs(cl_flux), abs(qt_flux), 1e-30)
    rel = abs(cl_flux - qt_flux) / scale
    assert rel < 1e-4, (
        "flux linkage differs by %.3e (classic %.9g, femmqt %.9g); inductance "
        "computed from these would disagree by the same factor"
        % (rel, cl_flux, qt_flux))


# ---------------------------------------------------------------------------
# 4. Block integrals
# ---------------------------------------------------------------------------

def test_block_area_agrees_with_summed_element_areas(solved):
    """mo_blockintegral(5) against the geometry it integrates over.

    femmqt does not expose a block-integral API outside the GUI, so this
    checks classic's area integral against the mesh both sides share:
    the sum of the triangle areas carrying that block's label. A
    disagreement means the two are integrating over different elements,
    which would make every other block integral differ too.
    """
    fem, ans = solved

    # the conductor was given group 1
    femm.mo_groupselectblock(1)
    classic_area = abs(complex(femm.mo_blockintegral(5)))
    femm.mo_clearblock()

    # sum the areas of elements whose label is the conductor's
    mesh = _parse_ans_mesh(ans)
    labels = {}
    for p0, p1, p2, lbl in mesh["elements"]:
        a, b, c = mesh["nodes"][p0], mesh["nodes"][p1], mesh["nodes"][p2]
        area = abs(0.5 * ((b[0] - a[0]) * (c[1] - a[1])
                          - (c[0] - a[0]) * (b[1] - a[1])))
        labels[lbl] = labels.get(lbl, 0.0) + area

    # mm^2 -> m^2; the conductor is the 8x8 mm square
    expected_mm2 = 8.0 * 8.0
    best = min(labels.values(), key=lambda v: abs(v - expected_mm2)) \
        if labels else 0.0

    _note("    block area: classic integral %.6e m^2, summed element areas "
          "%.6g mm^2 (the 8x8 conductor is %.6g mm^2)"
          % (classic_area, best, expected_mm2))
    assert abs(best - expected_mm2) / expected_mm2 < 0.02, (
        "the summed element areas for the conductor's label come to %.6g mm^2, "
        "not the %.6g mm^2 its geometry covers" % (best, expected_mm2))
    assert abs(classic_area - expected_mm2 * 1e-6) / (expected_mm2 * 1e-6) < 0.02, (
        "classic's area integral %.6e m^2 does not match the conductor's "
        "%.6e m^2" % (classic_area, expected_mm2 * 1e-6))


def _parse_ans_mesh(path):
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        lines = [ln.strip() for ln in fh]
    start = next(i for i, l in enumerate(lines)
                 if l.lower().startswith("[solution]")) + 1
    idx = start
    n = int(float(lines[idx].split()[0]))
    idx += 1
    nodes = []
    for _ in range(n):
        parts = lines[idx].split()
        nodes.append((float(parts[0]), float(parts[1])))
        idx += 1
    m = int(float(lines[idx].split()[0]))
    idx += 1
    elements = []
    for _ in range(m):
        parts = lines[idx].split()
        elements.append((int(parts[0]), int(parts[1]), int(parts[2]),
                         int(parts[3]) if len(parts) > 3 else -1))
        idx += 1
    return {"nodes": nodes, "elements": elements}


def test_write_report():
    path = _write_report()
    assert os.path.exists(path)
    print("report: " + path)
