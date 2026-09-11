"""
Mesh generation regression tests (issue #15).

MeshBuilder/Triangle drives every solved result downstream, and nothing
asserted that it produces a usable mesh. A mesher or parameter change
that quietly halves the element count, or emits a sliver, changes every
number this suite checks elsewhere without failing anything here.

Where the mesh is read from: `mi_createmesh()` returns the element count
but leaves no .node/.ele behind for inspection (the solver consumes and
deletes them). The solved .ans carries the whole mesh instead -- node
coordinates followed by element connectivity -- so that is what these
tests parse. It has the additional advantage of being the mesh the solver
actually used, not one regenerated afterwards.

Reference counts are a TOLERANCE BAND, not exact numbers. Triangle's
output depends on its own refinement heuristics and on floating-point
detail; a band makes a real change (a halved count, a mesher swap)
visible while tolerating the last-digit wobble that would otherwise make
this file flake.

Requirements: a built, COM-registered femmx.exe; pip install pyfemm pywin32.

Usage:
    pytest test/mesh_generation_test.py -v
"""

import math
import os

import pytest

import femm

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "results", "mesh_generation")

# The band a count must stay inside, as a fraction of the reference.
COUNT_TOLERANCE = 0.25

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
    path = os.path.join(OUTPUT_DIR, "mesh_generation.txt")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("Mesh generation checks" + os.linesep)
        fh.write("=" * 70 + os.linesep)
        for line in _REPORT:
            fh.write(line + os.linesep)
    return path


# ---------------------------------------------------------------------------
# Reading the mesh out of a solved .ans
# ---------------------------------------------------------------------------

def parse_ans_mesh(path):
    """{nodes: [(x, y)], elements: [(p0, p1, p2, label)]}

    The .ans mesh section is a node count, that many "x y Are [Aim]" rows,
    an element count, then that many "p0 p1 p2 label" rows -- the same
    layout femmqt/AnsFileIO.cpp reads.
    """
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        lines = [ln.strip() for ln in fh]

    # the mesh follows the [Solution] marker
    start = None
    for i, line in enumerate(lines):
        if line.lower().startswith("[solution]"):
            start = i + 1
            break
    assert start is not None, "%s has no [Solution] section" % path

    idx = start
    node_count = int(float(lines[idx].split()[0]))
    idx += 1
    nodes = []
    for _ in range(node_count):
        parts = lines[idx].split()
        nodes.append((float(parts[0]), float(parts[1])))
        idx += 1

    elem_count = int(float(lines[idx].split()[0]))
    idx += 1
    elements = []
    for _ in range(elem_count):
        parts = lines[idx].split()
        elements.append((int(parts[0]), int(parts[1]), int(parts[2]),
                         int(parts[3]) if len(parts) > 3 else -1))
        idx += 1

    return {"nodes": nodes, "elements": elements}


def triangle_angles(a, b, c):
    """The three interior angles, in degrees."""
    def side(p, q):
        return math.hypot(p[0] - q[0], p[1] - q[1])
    la, lb, lc = side(b, c), side(a, c), side(a, b)
    out = []
    for opposite, s1, s2 in ((la, lb, lc), (lb, la, lc), (lc, la, lb)):
        denom = 2.0 * s1 * s2
        if denom == 0.0:
            return [0.0, 0.0, 0.0]
        cosv = (s1 * s1 + s2 * s2 - opposite * opposite) / denom
        cosv = max(-1.0, min(1.0, cosv))
        out.append(math.degrees(math.acos(cosv)))
    return out


def signed_area(a, b, c):
    return 0.5 * ((b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]))


# ---------------------------------------------------------------------------
# Models
# ---------------------------------------------------------------------------

def _square_model(path, mesh_size, min_angle=30.0, side=20.0):
    """A plain square of air, meshed at the requested size."""
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    femm.openfemm(1)
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-8, 1, min_angle)
    femm.mi_smartmesh(0)
    femm.mi_getmaterial("Air")

    for x, y in ((0, 0), (side, 0), (side, side), (0, side)):
        femm.mi_addnode(x, y)
    femm.mi_addsegment(0, 0, side, 0)
    femm.mi_addsegment(side, 0, side, side)
    femm.mi_addsegment(side, side, 0, side)
    femm.mi_addsegment(0, side, 0, 0)

    femm.mi_addboundprop("edge", 0, 0, 0, 0, 0, 0, 0, 0, 0)
    for px, py in ((side / 2, 0), (side, side / 2),
                   (side / 2, side), (0, side / 2)):
        femm.mi_selectsegment(px, py)
        femm.mi_setsegmentprop("edge", 0, 1, 0, 0)
        femm.mi_clearselected()

    femm.mi_addblocklabel(side / 2, side / 2)
    femm.mi_selectlabel(side / 2, side / 2)
    femm.mi_setblockprop("Air", 0, mesh_size, "", 0, 0, 0)
    femm.mi_attachdefault()
    femm.mi_clearselected()

    femm.mi_zoomnatural()
    femm.mi_saveas(_save(path))
    femm.mi_analyze(1)
    return path[:-4] + ".ans"


def _meshed(path, mesh_size, min_angle=30.0, side=20.0):
    ans = _square_model(path, mesh_size, min_angle, side)
    assert os.path.exists(ans), "analyze produced no .ans for %s" % path
    return parse_ans_mesh(ans)


# ---------------------------------------------------------------------------
# 1. Counts stay in a band
# ---------------------------------------------------------------------------

# Measured on a known-good build (32-bit Triangle, smart mesh off) and
# asserted within COUNT_TOLERANCE, so a mesher or parameter change is
# visible rather than silent. Update these deliberately, as part of the
# change that moves them.
REFERENCE_COUNTS = {
    "coarse": {"mesh_size": 5.0, "nodes": 24, "elements": 30},
    "medium": {"mesh_size": 2.5, "nodes": 84, "elements": 135},
    "fine": {"mesh_size": 1.25, "nodes": 307, "elements": 548},
}


@pytest.mark.parametrize("label", sorted(REFERENCE_COUNTS))
def test_mesh_counts_stay_within_the_reference_band(label):
    """A mesher or parameter change must move counts visibly, not silently."""
    spec = REFERENCE_COUNTS[label]
    path = os.path.join(OUTPUT_DIR, "counts_%s.fem" % label)
    try:
        mesh = _meshed(path, spec["mesh_size"])
    finally:
        _teardown()

    n, e = len(mesh["nodes"]), len(mesh["elements"])
    _note("    %-7s mesh_size=%-5.2f -> %5d nodes, %5d elements "
          "(reference %d / %d)"
          % (label, spec["mesh_size"], n, e, spec["nodes"], spec["elements"]))

    for got, want, what in ((n, spec["nodes"], "node"),
                            (e, spec["elements"], "element")):
        lo = want * (1.0 - COUNT_TOLERANCE)
        hi = want * (1.0 + COUNT_TOLERANCE)
        assert lo <= got <= hi, (
            "%s: %s count %d is outside the %.0f%% band around the reference "
            "%d (%.0f..%.0f). If the mesher or its parameters changed on "
            "purpose, update REFERENCE_COUNTS in this file as part of that "
            "change." % (label, what, got, COUNT_TOLERANCE * 100, want, lo, hi))


# ---------------------------------------------------------------------------
# 2. Mesh quality
# ---------------------------------------------------------------------------

def test_no_degenerate_or_inverted_elements():
    """Zero-area and inverted triangles break the solve silently."""
    path = os.path.join(OUTPUT_DIR, "quality.fem")
    try:
        mesh = _meshed(path, 2.0)
    finally:
        _teardown()

    nodes = mesh["nodes"]
    zero_area = 0
    inverted = 0
    smallest = float("inf")
    for p0, p1, p2, _ in mesh["elements"]:
        a, b, c = nodes[p0], nodes[p1], nodes[p2]
        area = signed_area(a, b, c)
        smallest = min(smallest, abs(area))
        if abs(area) < 1e-12:
            zero_area += 1
        elif area < 0:
            inverted += 1

    _note("    quality: %d elements, smallest |area| %.3e, %d zero-area, "
          "%d inverted" % (len(mesh["elements"]), smallest, zero_area, inverted))
    assert zero_area == 0, "%d zero-area elements" % zero_area
    # Triangle emits consistently-wound triangles; a mix means something
    # reordered connectivity.
    assert inverted == 0 or inverted == len(mesh["elements"]), (
        "%d of %d elements are wound the other way -- the winding is "
        "inconsistent, which is worse than either convention"
        % (inverted, len(mesh["elements"])))


def test_minimum_angle_respects_the_configured_bound():
    """The mesher is asked for a minimum angle; it must deliver it."""
    path = os.path.join(OUTPUT_DIR, "minangle.fem")
    requested = 30.0
    try:
        mesh = _meshed(path, 2.0, min_angle=requested)
    finally:
        _teardown()

    nodes = mesh["nodes"]
    worst = 180.0
    below = 0
    for p0, p1, p2, _ in mesh["elements"]:
        angles = triangle_angles(nodes[p0], nodes[p1], nodes[p2])
        m = min(angles)
        worst = min(worst, m)
        if m < requested - 1.0:     # 1 degree of slack for round-off
            below += 1

    fraction = below / float(len(mesh["elements"]))
    _note("    min angle: requested %.0f deg, worst %.2f deg, %d/%d elements "
          "below (%.2f%%)"
          % (requested, worst, below, len(mesh["elements"]), fraction * 100))
    # Triangle cannot always honour the bound at fixed input corners, so a
    # handful of exceptions is expected; a large fraction is not.
    assert fraction < 0.05, (
        "%.1f%% of elements are below the requested %.0f degree minimum "
        "angle (worst %.2f) -- the quality constraint is not being applied"
        % (fraction * 100, requested, worst))


def test_every_node_is_referenced_by_an_element():
    """An unreferenced node means the mesh and the node list disagree."""
    path = os.path.join(OUTPUT_DIR, "orphans.fem")
    try:
        mesh = _meshed(path, 2.0)
    finally:
        _teardown()

    used = set()
    for p0, p1, p2, _ in mesh["elements"]:
        used.update((p0, p1, p2))
    orphans = [i for i in range(len(mesh["nodes"])) if i not in used]

    _note("    referencing: %d nodes, %d referenced, %d orphaned"
          % (len(mesh["nodes"]), len(used), len(orphans)))
    assert not orphans, (
        "%d mesh node(s) are not referenced by any element, first few %r"
        % (len(orphans), orphans[:5]))
    for p0, p1, p2, _ in mesh["elements"]:
        for p in (p0, p1, p2):
            assert 0 <= p < len(mesh["nodes"]), (
                "element references node %d, outside 0..%d"
                % (p, len(mesh["nodes"]) - 1))


# ---------------------------------------------------------------------------
# 3. Density controls do what they claim
# ---------------------------------------------------------------------------

def test_smaller_mesh_size_produces_more_elements():
    """The per-block mesh size must actually control density, monotonically."""
    results = []
    for size in (6.0, 3.0, 1.5):
        path = os.path.join(OUTPUT_DIR, "density_%g.fem" % size)
        try:
            mesh = _meshed(path, size)
        finally:
            _teardown()
        results.append((size, len(mesh["elements"])))

    _note("    density control: " + "  ".join("size=%.1f -> %d elems" % r
                                              for r in results))
    for (s0, e0), (s1, e1) in zip(results, results[1:]):
        assert e1 > e0, (
            "halving the mesh size from %.1f to %.1f did not increase the "
            "element count (%d -> %d); the per-block mesh size is not being "
            "applied" % (s0, s1, e0, e1))
    # and the effect should be substantial, not a rounding difference
    assert results[-1][1] > results[0][1] * 3, (
        "a 4x finer mesh size only moved the element count from %d to %d"
        % (results[0][1], results[-1][1]))


# ---------------------------------------------------------------------------
# 4. Crossing segments must produce the regions a user expects
# ---------------------------------------------------------------------------

def test_crossing_segments_mesh_into_separate_regions():
    """The point of crossing-segment node insertion, seen from the mesher.

    A square divided by a cross into four quadrants, each with its own
    block label. Without a node at the crossing the mesher sees no shared
    vertex there, the quadrants are not actually bounded, and the labels
    do not partition the mesh. With it, every element belongs to exactly
    one of the four.
    """
    path = os.path.join(OUTPUT_DIR, "quadrants.fem")
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    side = 20.0
    try:
        femm.openfemm(1)
        femm.newdocument(0)
        femm.mi_probdef(0, "millimeters", "planar", 1e-8, 1, 30)
        femm.mi_smartmesh(0)
        femm.mi_getmaterial("Air")
        femm.mi_getmaterial("Copper")

        for x, y in ((0, 0), (side, 0), (side, side), (0, side)):
            femm.mi_addnode(x, y)
        femm.mi_addsegment(0, 0, side, 0)
        femm.mi_addsegment(side, 0, side, side)
        femm.mi_addsegment(side, side, 0, side)
        femm.mi_addsegment(0, side, 0, 0)

        # The dividing cross. mi_addsegment connects the nearest EXISTING
        # nodes to the coordinates it is given -- it does not create them.
        # Without these four, both dividers snapped to the corners and
        # collapsed into a single diagonal, and two of the four block
        # labels were dropped as a result.
        for x, y in ((0, side / 2), (side, side / 2),
                     (side / 2, 0), (side / 2, side)):
            femm.mi_addnode(x, y)
        femm.mi_addsegment(0, side / 2, side, side / 2)
        femm.mi_addsegment(side / 2, 0, side / 2, side)

        femm.mi_addboundprop("edge", 0, 0, 0, 0, 0, 0, 0, 0, 0)
        for px, py in ((side / 2, 0), (side, side / 2),
                       (side / 2, side), (0, side / 2)):
            femm.mi_selectsegment(px, py)
            femm.mi_setsegmentprop("edge", 0, 1, 0, 0)
            femm.mi_clearselected()

        quadrants = [(5, 5), (15, 5), (15, 15), (5, 15)]
        for i, (qx, qy) in enumerate(quadrants):
            femm.mi_addblocklabel(qx, qy)
            femm.mi_selectlabel(qx, qy)
            femm.mi_setblockprop("Air" if i % 2 == 0 else "Copper",
                                 0, 2.0, "", 0, 0, 0)
            femm.mi_clearselected()

        femm.mi_zoomnatural()
        femm.mi_saveas(_save(path))
        femm.mi_analyze(1)
        mesh = parse_ans_mesh(path[:-4] + ".ans")
    finally:
        _teardown()

    labels = {}
    for _, _, _, lbl in mesh["elements"]:
        labels[lbl] = labels.get(lbl, 0) + 1

    _note("    quadrants: %d elements across %d distinct block labels %r"
          % (len(mesh["elements"]), len(labels), sorted(labels)))
    assert len(labels) >= 4, (
        "the four labelled quadrants produced only %d distinct element "
        "labels %r -- the dividing segments did not partition the region, "
        "which is what a missing node at the crossing looks like"
        % (len(labels), sorted(labels)))
    # and each quadrant should hold a comparable share
    counts = sorted(labels.values())
    assert counts[0] > counts[-1] * 0.25, (
        "quadrant element counts are lopsided %r; the divide did not land "
        "where it should" % counts)


# ---------------------------------------------------------------------------
# 5. The two Triangle builds
# ---------------------------------------------------------------------------

def test_triangle_build_variant_is_recorded():
    """CI always passes -ForceTriangle32bit; note which build is present.

    The tree carries both triangle/ and triangle64/, but a given build
    produces one triangle.exe. Comparing the two meshers directly needs
    both binaries side by side, which this build layout does not provide,
    so this records what was actually exercised rather than pretending to
    a comparison it cannot make. See test/README.md.
    """
    bin_dir = os.path.join(os.path.dirname(SCRIPT_DIR), "bin", "plain")
    exe = os.path.join(bin_dir, "triangle.exe")
    if not os.path.exists(exe):
        pytest.skip("no triangle.exe at %s" % exe)

    with open(exe, "rb") as fh:
        head = fh.read(0x400)
    # PE machine type: 0x8664 = x64, 0x014c = x86
    pe_off = int.from_bytes(head[0x3C:0x40], "little")
    machine = int.from_bytes(head[pe_off + 4:pe_off + 6], "little")
    kind = {0x8664: "x64", 0x014C: "x86"}.get(machine, "0x%04X" % machine)
    _note("    triangle.exe is a %s build (%d bytes)"
          % (kind, os.path.getsize(exe)))
    assert machine in (0x8664, 0x014C), "unrecognised PE machine type"


def test_write_report():
    path = _write_report()
    assert os.path.exists(path)
    print("report: " + path)
