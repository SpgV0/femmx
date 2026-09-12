"""
DXF import/export coverage for both implementations (issue #8).

DXF is how real geometry gets into FEMM from CAD, and there are two
independent implementations in this tree: classic FEMM's
MOVECOPY.CPP ReadDXF/WriteDXF, and femmqt's DxfIO, a direct port of it.
Neither had a single test -- the Lua sweep writes a .dxf but never reads
one back or looks at what it wrote.

The fixtures under test/fixtures/dxf/ are small, hand-written and
committed: one entity type per file, so a failure names the entity rather
than pointing at a large opaque drawing.

Cross-implementation comparison needs femmqt's parser reachable without
driving the GUI, so this ships alongside a `femmqt.exe --import-dxf
<in.dxf> <out.fem> [tolerance]` CLI mode, mirroring the existing
--convert-ansx.

Requirements: a built, COM-registered femmx.exe; pip install pyfemm pywin32.

Usage:
    pytest test/dxf_roundtrip_test.py -v
"""

import math
import os
import subprocess

import pytest

import femm

import femmx_paths

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
FIXTURE_DIR = os.path.join(SCRIPT_DIR, "fixtures", "dxf")
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "results", "dxf_roundtrip")

FEMMQT = os.path.join(femmx_paths.BIN_DIR, "femmqt.exe")

POS_TOL = 1e-6

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
    path = os.path.join(OUTPUT_DIR, "dxf_roundtrip.txt")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("DXF import/export checks" + os.linesep)
        fh.write("=" * 70 + os.linesep)
        for line in _REPORT:
            fh.write(line + os.linesep)
    return path


def fixture(name):
    path = os.path.join(FIXTURE_DIR, name)
    assert os.path.exists(path), "missing fixture %s" % path
    return path


# ---------------------------------------------------------------------------
# Reading geometry back out of FEMM
# ---------------------------------------------------------------------------

def _open_blank_magnetics():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    femm.openfemm(1)
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-9, 1, 30)


def _readdxf(path):
    """mi_readdxf, tolerating the importer's "orphaned end points" advisory.

    Classic FEMM reports, after a successful import, any endpoint that is
    not shared with another entity -- a genuinely useful warning about
    geometry that will not mesh as the user expects. It is advisory, not a
    failure: the entities are imported either way. pyfemm turns every FEMM
    message into an exception, though, so an open shape (a lone LINE, a lone
    ARC, an unclosed POLYLINE) would otherwise look like an import error.
    Anything that is NOT that advisory is still raised.
    """
    try:
        femm.mi_readdxf(_save(path))
        return None
    except Exception as exc:  # noqa: BLE001
        text = str(exc)
        if "Orphaned" in text or "orphan" in text.lower():
            return text.split(chr(10))[0].strip()
        raise


def _geometry_via_save(tag):
    """Save the current model and parse its geometry tables back."""
    path = os.path.join(OUTPUT_DIR, "%s.fem" % tag)
    if os.path.exists(path):
        os.remove(path)
    femm.mi_saveas(_save(path))
    return parse_geometry(path), path


def parse_geometry(fem_path):
    """{nodes: [(x,y)], segments: [(n0,n1)], arcs: [(n0,n1,angle)]}"""
    with open(fem_path, "r", encoding="utf-8", errors="replace") as fh:
        lines = [ln.rstrip("\n").rstrip("\r") for ln in fh]

    out = {"nodes": [], "segments": [], "arcs": []}
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        if line.startswith("[NumPoints]"):
            n = int(float(line.split("=", 1)[1]))
            for j in range(i + 1, i + 1 + n):
                parts = lines[j].split()
                out["nodes"].append((float(parts[0]), float(parts[1])))
            i += 1 + n
            continue
        if line.startswith("[NumSegments]"):
            n = int(float(line.split("=", 1)[1]))
            for j in range(i + 1, i + 1 + n):
                parts = lines[j].split()
                out["segments"].append((int(parts[0]), int(parts[1])))
            i += 1 + n
            continue
        if line.startswith("[NumArcSegments]"):
            n = int(float(line.split("=", 1)[1]))
            for j in range(i + 1, i + 1 + n):
                parts = lines[j].split()
                out["arcs"].append((int(parts[0]), int(parts[1]), float(parts[2])))
            i += 1 + n
            continue
        i += 1
    return out


def _edges_as_points(geom):
    """Segments/arcs as sorted coordinate pairs, so node ORDER cannot matter."""
    nodes = geom["nodes"]
    edges = []
    for n0, n1 in geom["segments"]:
        a, b = nodes[n0], nodes[n1]
        edges.append(("seg",) + tuple(sorted([a, b])))
    for n0, n1, angle in geom["arcs"]:
        a, b = nodes[n0], nodes[n1]
        # an arc and its reverse are the same curve; normalise by endpoint order
        pts = sorted([a, b])
        edges.append(("arc", pts[0], pts[1], round(abs(angle), 6)))
    return sorted(edges)


def _points_close(a, b, tol=POS_TOL):
    return math.hypot(a[0] - b[0], a[1] - b[1]) <= tol


def _describe(geom):
    return ("%d nodes, %d segments, %d arcs"
            % (len(geom["nodes"]), len(geom["segments"]), len(geom["arcs"])))


# ---------------------------------------------------------------------------
# 1. Export -> import round-trip through the classic implementation
# ---------------------------------------------------------------------------

def test_classic_export_import_roundtrip():
    """Geometry drawn in FEMM, exported to DXF and re-imported, must match."""
    _open_blank_magnetics()
    try:
        # a shape using both entity types the exporter writes: LINE and ARC
        for x, y in ((0, 0), (20, 0), (20, 12), (0, 12)):
            femm.mi_addnode(x, y)
        femm.mi_addsegment(0, 0, 20, 0)
        femm.mi_addsegment(20, 0, 20, 12)
        femm.mi_addsegment(20, 12, 0, 12)
        femm.mi_addsegment(0, 12, 0, 0)
        femm.mi_addnode(6, 6)
        femm.mi_addnode(12, 6)
        femm.mi_addarc(6, 6, 12, 6, 90, 1)

        before, _ = _geometry_via_save("roundtrip_before")
        dxf_path = os.path.join(OUTPUT_DIR, "roundtrip.dxf")
        if os.path.exists(dxf_path):
            os.remove(dxf_path)
        femm.mi_savedxf(_save(dxf_path))
    finally:
        _teardown()

    assert os.path.exists(dxf_path), "mi_savedxf produced no file"

    _open_blank_magnetics()
    try:
        _readdxf(dxf_path)
        after, _ = _geometry_via_save("roundtrip_after")
    finally:
        _teardown()

    _note("    classic export/import: before %s -> after %s"
          % (_describe(before), _describe(after)))

    # DXF carries no block labels or properties, so compare geometry only,
    # and compare edges by coordinates rather than by node index: the
    # importer is free to renumber.
    before_edges = _edges_as_points(before)
    after_edges = _edges_as_points(after)
    assert len(after_edges) == len(before_edges), (
        "edge count changed across the DXF round-trip: %d -> %d%s  before=%r%s  after=%r"
        % (len(before_edges), len(after_edges), os.linesep, before_edges,
           os.linesep, after_edges))
    for want, got in zip(before_edges, after_edges):
        assert want[0] == got[0], "entity kind changed: %r -> %r" % (want, got)
        assert _points_close(want[1], got[1]) and _points_close(want[2], got[2]), (
            "endpoint moved across the round-trip: %r -> %r" % (want, got))


# ---------------------------------------------------------------------------
# 2. Entity coverage, one fixture per type
# ---------------------------------------------------------------------------

ENTITY_CASES = [
    # name,            file,             min_nodes, min_segments, min_arcs
    ("LINE", "line.dxf", 3, 2, 0),
    ("ARC", "arc.dxf", 2, 0, 1),
    ("CIRCLE", "circle.dxf", 1, 0, 1),
    ("LWPOLYLINE", "lwpolyline.dxf", 4, 4, 0),
    ("POLYLINE", "polyline.dxf", 3, 2, 0),
]


@pytest.mark.parametrize("label,filename,min_nodes,min_segs,min_arcs", ENTITY_CASES)
def test_classic_imports_entity_type(label, filename, min_nodes, min_segs, min_arcs):
    """Each DXF entity type the importer claims to handle must import."""
    _open_blank_magnetics()
    try:
        _readdxf(fixture(filename))
        geom, _ = _geometry_via_save("entity_%s" % label.lower())
    finally:
        _teardown()

    _note("    %-11s %s -> %s" % (label, filename, _describe(geom)))
    assert len(geom["nodes"]) >= min_nodes, (
        "%s: expected at least %d nodes, got %d"
        % (label, min_nodes, len(geom["nodes"])))
    assert len(geom["segments"]) >= min_segs, (
        "%s: expected at least %d segments, got %d"
        % (label, min_segs, len(geom["segments"])))
    assert len(geom["arcs"]) >= min_arcs, (
        "%s: expected at least %d arcs, got %d"
        % (label, min_arcs, len(geom["arcs"])))


def test_arc_geometry_is_correct():
    """arc.dxf is a quarter circle r=10 centred at the origin."""
    _open_blank_magnetics()
    try:
        _readdxf(fixture("arc.dxf"))
        geom, _ = _geometry_via_save("arc_detail")
    finally:
        _teardown()

    assert len(geom["arcs"]) == 1, "expected exactly one arc, got %d" % len(geom["arcs"])
    n0, n1, included = geom["arcs"][0]
    a, b = geom["nodes"][n0], geom["nodes"][n1]
    _note("    arc endpoints %r %r, included angle %.3f deg" % (a, b, included))

    for pt in (a, b):
        r = math.hypot(pt[0], pt[1])
        assert abs(r - 10.0) < 1e-6, (
            "arc endpoint %r is at radius %.9f, expected 10" % (pt, r))
    assert abs(included - 90.0) < 1e-6, (
        "included angle is %.9f deg, expected 90" % included)


def test_circle_becomes_closed_arcs():
    """A CIRCLE has no endpoints, so it must arrive as arcs summing to 360."""
    _open_blank_magnetics()
    try:
        _readdxf(fixture("circle.dxf"))
        geom, _ = _geometry_via_save("circle_detail")
    finally:
        _teardown()

    total = sum(abs(a[2]) for a in geom["arcs"])
    _note("    circle.dxf -> %d arc(s) totalling %.3f deg" % (len(geom["arcs"]), total))
    assert geom["arcs"], "a CIRCLE entity produced no arcs"
    assert abs(total - 360.0) < 1e-6, (
        "arcs from a CIRCLE total %.6f degrees, expected 360" % total)
    for node in geom["nodes"]:
        r = math.hypot(node[0] - 5.0, node[1] - 5.0)
        assert abs(r - 3.0) < 1e-6, (
            "node %r is %.9f from the circle centre, expected 3" % (node, r))


def test_layers_become_groups():
    """DXF layers must map onto distinct FEMM group numbers.

    Classic FEMM builds a layer list from the LAYER entries in the TABLES
    section and uses each layer's INDEX in that list as the group number
    (MOVECOPY.CPP). An entity's group-8 layer attribute on its own is not
    enough: if the layer was never declared in TABLES the lookup finds
    nothing and every entity silently lands in group 0. layers.dxf
    therefore declares PARTA/PARTB/PARTC properly.
    """
    _open_blank_magnetics()
    try:
        _readdxf(fixture("layers.dxf"))
        geom, path = _geometry_via_save("layers")
    finally:
        _teardown()

    groups = _segment_groups(path)
    _note("    layers.dxf (3 declared layers) -> %d segments in groups %s"
          % (len(geom["segments"]), sorted(groups)))
    assert len(geom["segments"]) == 3, (
        "expected the three LINEs to import, got %d segments"
        % len(geom["segments"]))
    assert len(groups) >= 2, (
        "three DXF layers collapsed onto group(s) %s; layers are supposed "
        "to become distinct group numbers" % sorted(groups))


def _segment_groups(fem_path):
    """Group numbers from the segment table.

    Segment rows are: n0 n1 MaxSideLength boundaryPropIndex hidden inGroup
    (femm/FemmeDoc.cpp writes exactly these six).
    """
    with open(fem_path, "r", encoding="utf-8", errors="replace") as fh:
        lines = [ln.rstrip("\n").rstrip("\r") for ln in fh]
    groups = set()
    for i, line in enumerate(lines):
        if line.strip().startswith("[NumSegments]"):
            n = int(float(line.split("=", 1)[1]))
            for j in range(i + 1, i + 1 + n):
                parts = lines[j].split()
                if len(parts) >= 6:
                    groups.add(int(float(parts[5])))
            break
    return groups

def _is_number(token):
    try:
        float(token)
        return True
    except ValueError:
        return False


# ---------------------------------------------------------------------------
# 3. Units: neither implementation reads $INSUNITS
# ---------------------------------------------------------------------------

def test_insunits_is_ignored_and_coordinates_are_taken_at_face_value():
    """Documents a real gap so a future change to honour it is deliberate.

    inches.dxf declares $INSUNITS = 1 (inches) and draws a 1-unit line.
    Neither implementation looks at $INSUNITS, so in a millimetre problem
    that line arrives 1 mm long, not 25.4 mm. Asserting the CURRENT
    behaviour means the day someone implements unit conversion, this test
    fails and forces the decision to be conscious.
    """
    _open_blank_magnetics()
    try:
        _readdxf(fixture("inches.dxf"))
        geom, _ = _geometry_via_save("inches")
    finally:
        _teardown()

    xs = [n[0] for n in geom["nodes"]]
    span = max(xs) - min(xs)
    _note("    inches.dxf ($INSUNITS=1) in a millimetre problem -> span %.6f "
          "(25.4 would mean units were honoured)" % span)
    assert abs(span - 1.0) < 1e-6, (
        "a 1-unit line from a $INSUNITS=1 (inches) DXF spans %.6f in a "
        "millimetre problem. If unit conversion was just implemented, that "
        "is a behaviour change this test exists to make visible." % span)


# ---------------------------------------------------------------------------
# 4. Both implementations must agree on the same fixture
# ---------------------------------------------------------------------------

def _femmqt_import(fixture_name, tag, tolerance=None):
    out = os.path.join(OUTPUT_DIR, "%s_qt.fem" % tag)
    if os.path.exists(out):
        os.remove(out)
    cmd = [FEMMQT, "--import-dxf", fixture(fixture_name), out]
    if tolerance is not None:
        cmd.append(str(tolerance))
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    return proc, out


@pytest.mark.parametrize("filename", ["square.dxf", "line.dxf", "arc.dxf",
                                      "circle.dxf", "lwpolyline.dxf"])
def test_both_implementations_agree(filename):
    """classic ReadDXF and femmqt DxfIO must produce the same geometry."""
    if not os.path.exists(FEMMQT):
        pytest.skip("femmqt.exe not built at %s" % FEMMQT)

    tag = os.path.splitext(filename)[0]
    _open_blank_magnetics()
    try:
        _readdxf(fixture(filename))
        classic, _ = _geometry_via_save("agree_%s_classic" % tag)
    finally:
        _teardown()

    proc, qt_path = _femmqt_import(filename, "agree_%s" % tag, tolerance=0)
    assert proc.returncode == 0, (
        "femmqt --import-dxf failed on %s: %s" % (filename, proc.stderr.strip()))
    qt = parse_geometry(qt_path)

    _note("    %-15s classic: %-34s femmqt: %s"
          % (filename, _describe(classic), _describe(qt)))

    # Compare the geometry as unordered coordinate sets: the two parsers are
    # free to emit entities in different order or number nodes differently.
    classic_edges = _edges_as_points(classic)
    qt_edges = _edges_as_points(qt)
    assert len(classic_edges) == len(qt_edges), (
        "%s: classic produced %d edges, femmqt produced %d"
        % (filename, len(classic_edges), len(qt_edges)))
    for want, got in zip(classic_edges, qt_edges):
        assert want[0] == got[0], (
            "%s: entity kind differs: classic %r vs femmqt %r"
            % (filename, want, got))
        assert _points_close(want[1], got[1]) and _points_close(want[2], got[2]), (
            "%s: endpoints differ: classic %r vs femmqt %r" % (filename, want, got))


def test_default_tolerance_merges_coincident_nodes_like_classic():
    """With its suggested tolerance, femmqt must merge like classic does.

    square.dxf is four separate LINE entities whose endpoints coincide
    exactly. Classic applies a merge tolerance during import and produces
    four nodes; femmqt asked for tolerance 0 leaves all eight, which is why
    the agreement tests above compare EDGES by coordinate rather than node
    counts. Letting femmqt pick its own suggested tolerance -- the same
    value classic's import dialog auto-fills -- must reproduce classic's
    four.
    """
    if not os.path.exists(FEMMQT):
        pytest.skip("femmqt.exe not built at %s" % FEMMQT)

    _open_blank_magnetics()
    try:
        _readdxf(fixture("square.dxf"))
        classic, _ = _geometry_via_save("merge_classic")
    finally:
        _teardown()

    proc, qt_path = _femmqt_import("square.dxf", "merge_default")   # no tolerance
    assert proc.returncode == 0, proc.stderr.strip()
    qt = parse_geometry(qt_path)

    _note("    square.dxf merge: classic %s | femmqt (suggested tolerance) %s"
          % (_describe(classic), _describe(qt)))
    assert len(qt["nodes"]) == len(classic["nodes"]), (
        "classic merged the square down to %d nodes but femmqt left %d at "
        "its own suggested tolerance"
        % (len(classic["nodes"]), len(qt["nodes"])))
    assert len(qt["segments"]) == len(classic["segments"])

# ---------------------------------------------------------------------------
# 5. Malformed input must not crash
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("filename", ["truncated.dxf", "garbage.dxf"])
def test_malformed_dxf_does_not_crash_classic(filename):
    """A truncated or nonsense DXF must fail cleanly, not take the process."""
    _open_blank_magnetics()
    try:
        try:
            femm.mi_readdxf(_save(fixture(filename)))
            outcome = "returned without raising"
        except Exception as exc:  # noqa: BLE001
            outcome = "raised: %s" % str(exc).replace(os.linesep, " ")[:110]
        # the session must still be alive and answering
        femm.mi_addnode(1.0, 1.0)
        femm.mi_zoomnatural()
        _note("    classic on %-14s %s (session still responsive)"
              % (filename, outcome))
    finally:
        _teardown()


@pytest.mark.parametrize("filename", ["truncated.dxf", "garbage.dxf"])
def test_malformed_dxf_does_not_crash_femmqt(filename):
    """femmqt's parser must exit cleanly rather than crash on bad input."""
    if not os.path.exists(FEMMQT):
        pytest.skip("femmqt.exe not built at %s" % FEMMQT)

    proc, _ = _femmqt_import(filename, "bad_%s" % os.path.splitext(filename)[0])
    _note("    femmqt  on %-14s exit=%d stderr=%r"
          % (filename, proc.returncode, proc.stderr.strip()[:80]))
    # Either outcome is acceptable -- parse it as empty, or reject it -- but
    # the process must terminate normally. On Windows a crash surfaces as a
    # large unsigned status such as 0xC0000005, never as 0 or 1.
    assert proc.returncode in (0, 1), (
        "femmqt --import-dxf on %s exited with %d (0x%X), which is a crash, "
        "not a clean rejection" % (filename, proc.returncode,
                                   proc.returncode & 0xFFFFFFFF))


def test_write_report():
    path = _write_report()
    assert os.path.exists(path)
    print("report: " + path)
