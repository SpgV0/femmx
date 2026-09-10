"""
Round-trip fidelity for .fem / .femx / .ans / .ansx (issue #7).

FILE_FORMATS.md documents four formats and the relationships between them
-- .femx is a binary cache of .fem, .ansx of .ans's mesh -- and nothing
verified any of it. Both GUIs and all four solvers read and write these
files, so a field written but not read back, or read back in the wrong
order, is a data-loss bug no other test would notice.

The contract this file holds FEMM to is the one FILE_FORMATS.md states:

  "Every numeric field in .femx/.ansx is a byte-for-byte copy of what the
   text parser would have produced from .fem/.ans -- opening a file via
   its cache and via its text source must always produce identical
   in-memory state."

  "Each cache's header records its source file's size (bytes) and mtime
   (seconds since epoch) ... any mismatch means stale."

Comparisons are made on PARSED STRUCTURES with float tolerances, never on
raw bytes, and a failure names the field that drifted -- byte comparison
would flag harmless formatting differences and say nothing useful when it
did.

Requirements: a built, COM-registered femmx.exe; pip install pyfemm pywin32.

Usage:
    pytest test/file_format_roundtrip_test.py -v
"""

import os
import shutil
import struct
import time

import pytest

import femm

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "results", "file_format_roundtrip")

FLOAT_TOL = 1e-9

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
    path = os.path.join(OUTPUT_DIR, "file_format_roundtrip.txt")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("File-format round-trip checks" + os.linesep)
        fh.write("=" * 70 + os.linesep)
        for line in _REPORT:
            fh.write(line + os.linesep)
    return path


# ---------------------------------------------------------------------------
# A semantic .fem parser
#
# Deliberately structural rather than line-by-line: scalars, named property
# blocks, and the count-prefixed geometry tables are each pulled out into
# comparable Python objects so a diff can name the field that moved.
# ---------------------------------------------------------------------------

BLOCK_STARTS = {
    "<BeginPoint>": ("point_props", "<EndPoint>"),
    "<BeginBdry>": ("bdry_props", "<EndBdry>"),
    "<BeginBlock>": ("block_props", "<EndBlock>"),
    "<BeginCircuit>": ("circuit_props", "<EndCircuit>"),
    "<BeginConductor>": ("conductor_props", "<EndConductor>"),
}

GEOMETRY_KEYS = ("NumPoints", "NumSegments", "NumArcSegments",
                 "NumHoles", "NumBlockLabels")


def parse_fem(path):
    """Parse a .fem/.fee/.feh/.fec into {scalars, blocks, geometry}."""
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        lines = [ln.rstrip("\n").rstrip("\r") for ln in fh]

    scalars = {}
    blocks = {name: [] for name, _ in BLOCK_STARTS.values()}
    geometry = {}

    i = 0
    while i < len(lines):
        line = lines[i].strip()
        if not line:
            i += 1
            continue

        opener = next((k for k in BLOCK_STARTS if line.startswith(k)), None)
        if opener:
            key, closer = BLOCK_STARTS[opener]
            record = {}
            i += 1
            while i < len(lines) and closer not in lines[i]:
                inner = lines[i].strip()
                if "=" in inner:
                    lhs, rhs = inner.split("=", 1)
                    record[lhs.strip()] = rhs.strip()
                elif inner:
                    # BH-curve / TK-curve rows: keep them in order
                    record.setdefault("_rows", []).append(inner.split())
                i += 1
            blocks[key].append(record)
            i += 1
            continue

        if line.startswith("[") and "]" in line and "=" in line:
            lhs, rhs = line.split("=", 1)
            key = lhs.strip().strip("[]").strip()
            value = rhs.strip()
            if key in GEOMETRY_KEYS:
                count = int(float(value))
                rows = []
                for j in range(i + 1, i + 1 + count):
                    if j < len(lines):
                        rows.append(lines[j].split())
                geometry[key] = rows
                i += 1 + count
                continue
            scalars[key] = value
            i += 1
            continue

        i += 1

    return {"scalars": scalars, "blocks": blocks, "geometry": geometry}


def _num(token):
    try:
        return float(token)
    except (TypeError, ValueError):
        return None


def _values_equal(a, b):
    fa, fb = _num(a), _num(b)
    if fa is not None and fb is not None:
        if fa == fb:
            return True
        scale = max(abs(fa), abs(fb), 1.0)
        return abs(fa - fb) / scale <= FLOAT_TOL
    return a == b


def diff_fem(left, right, ignore_scalars=()):
    """Semantic diff; returns a list of human-readable drift descriptions."""
    problems = []

    ls, rs = left["scalars"], right["scalars"]
    for key in sorted(set(ls) | set(rs)):
        if key in ignore_scalars:
            continue
        if key not in ls or key not in rs:
            problems.append("scalar [%s] present in only one file "
                            "(left=%r right=%r)"
                            % (key, ls.get(key), rs.get(key)))
        elif not _values_equal(ls[key], rs[key]):
            problems.append("scalar [%s] drifted: %r -> %r"
                            % (key, ls[key], rs[key]))

    for kind in sorted(left["blocks"]):
        lb, rb = left["blocks"][kind], right["blocks"][kind]
        if len(lb) != len(rb):
            problems.append("%s count changed: %d -> %d"
                            % (kind, len(lb), len(rb)))
            continue
        for idx, (lrec, rrec) in enumerate(zip(lb, rb)):
            for field in sorted(set(lrec) | set(rrec)):
                lv, rv = lrec.get(field), rrec.get(field)
                if field == "_rows":
                    if lv != rv and (lv is None or rv is None
                                     or len(lv) != len(rv)
                                     or any(not _values_equal(a, b)
                                            for ra, rb_ in zip(lv, rv)
                                            for a, b in zip(ra, rb_))):
                        problems.append("%s[%d] curve data drifted" % (kind, idx))
                elif not _values_equal(lv, rv):
                    problems.append("%s[%d].%s drifted: %r -> %r"
                                    % (kind, idx, field, lv, rv))

    for key in sorted(set(left["geometry"]) | set(right["geometry"])):
        lg = left["geometry"].get(key, [])
        rg = right["geometry"].get(key, [])
        if len(lg) != len(rg):
            problems.append("%s count changed: %d -> %d" % (key, len(lg), len(rg)))
            continue
        for idx, (lrow, rrow) in enumerate(zip(lg, rg)):
            if len(lrow) != len(rrow):
                problems.append("%s row %d field count changed: %d -> %d"
                                % (key, idx, len(lrow), len(rrow)))
                continue
            for col, (a, b) in enumerate(zip(lrow, rrow)):
                if not _values_equal(a, b):
                    problems.append("%s row %d column %d drifted: %r -> %r"
                                    % (key, idx, col, a, b))
    return problems


# ---------------------------------------------------------------------------
# Binary cache headers, per FILE_FORMATS.md
# ---------------------------------------------------------------------------

FEMX_MAGIC = b"FEMMFEMX"
ANSX_MAGIC = b"FEMMANSX"


def read_femx_header(path):
    """magic, version, headerSize, and the two freshness fields."""
    with open(path, "rb") as fh:
        blob = fh.read(1024)
    magic = blob[0:8]
    version, header_size = struct.unpack_from("<II", blob, 8)
    # sourceFemSize / sourceFemMtimeSecs sit immediately after the fixed
    # prevSoln[260] + comment[512] strings; locate them from headerSize by
    # walking back over the 8 count fields that end the header.
    tail = header_size - 8 * 8       # 8 uint64 counts at the end
    size, mtime = struct.unpack_from("<QQ", blob, tail - 16)
    return {"magic": magic, "version": version, "header_size": header_size,
            "source_size": size, "source_mtime": mtime}


def read_ansx_header(path):
    with open(path, "rb") as fh:
        blob = fh.read(80)
    magic = blob[0:8]
    version, header_size, coord, units = struct.unpack_from("<IIII", blob, 8)
    freq, bmin, bmax = struct.unpack_from("<ddd", blob, 24)
    size, mtime, nodes, elements = struct.unpack_from("<QQQQ", blob, 48)
    return {"magic": magic, "version": version, "header_size": header_size,
            "source_size": size, "source_mtime": mtime,
            "node_count": nodes, "element_count": elements}


# ---------------------------------------------------------------------------
# A model exercising every element type the ticket lists
# ---------------------------------------------------------------------------

def build_rich_model(path):
    """Nodes, segments, arcs, block labels, groups, a BH-curve material,
    boundary conditions, circuits and point properties -- all in one file."""
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    femm.openfemm(1)
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-9, 12.5, 30)

    femm.mi_getmaterial("Air")
    femm.mi_getmaterial("Copper")
    femm.mi_getmaterial("Pure Iron")          # carries a BH curve
    femm.mi_addcircprop("coil_a", 3.5, 1)
    femm.mi_addcircprop("coil_b", -1.25, 0)   # parallel, negative current
    femm.mi_addboundprop("fixedA", 0.5, 0, 0, 17.0, 0, 0, 0, 0, 0)
    femm.mi_addboundprop("mixed", 0, 0, 0, 0, 0, 0, 2.5, 1.5, 2)
    femm.mi_addpointprop("pp", 1.75, 0)

    # geometry: a rectangle, an arc-capped slot, and a circle
    for x, y in ((0, 0), (40, 0), (40, 25), (0, 25)):
        femm.mi_addnode(x, y)
    femm.mi_addsegment(0, 0, 40, 0)
    femm.mi_addsegment(40, 0, 40, 25)
    femm.mi_addsegment(40, 25, 0, 25)
    femm.mi_addsegment(0, 25, 0, 0)

    femm.mi_addnode(12, 12)
    femm.mi_addnode(20, 12)
    femm.mi_addarc(12, 12, 20, 12, 140.0, 1.0)
    femm.mi_addarc(20, 12, 12, 12, 220.0, 1.5)

    femm.mi_addnode(30, 18)
    femm.mi_addnode(34, 18)
    femm.mi_addarc(30, 18, 34, 18, 180, 2.0)
    femm.mi_addarc(34, 18, 30, 18, 180, 2.0)

    # properties on specific entities, including a non-zero group
    femm.mi_selectsegment(20, 0)
    femm.mi_setsegmentprop("fixedA", 0.75, 0, 0, 3)
    femm.mi_clearselected()
    femm.mi_selectsegment(0, 12.5)
    femm.mi_setsegmentprop("mixed", 0, 1, 1, 4)
    femm.mi_clearselected()
    femm.mi_selectnode(0, 0)
    femm.mi_setnodeprop("pp", 5)
    femm.mi_clearselected()

    femm.mi_addblocklabel(5, 5)
    femm.mi_selectlabel(5, 5)
    femm.mi_setblockprop("Air", 0, 2.5, "", 15.0, 7, 0)
    # Mark this as the default label, so any region the arc geometry
    # happens to carve out that is not explicitly labelled still gets a
    # material. Without it the solver refuses the model with "Material
    # properties have not been defined for all regions", which says
    # nothing about file-format fidelity -- the subject of this file.
    femm.mi_attachdefault()
    femm.mi_clearselected()

    femm.mi_addblocklabel(16, 12)
    femm.mi_selectlabel(16, 12)
    femm.mi_setblockprop("Pure Iron", 1, 0, "", 0, 8, 0)
    femm.mi_clearselected()

    femm.mi_addblocklabel(32, 18)
    femm.mi_selectlabel(32, 18)
    femm.mi_setblockprop("Copper", 1, 0, "coil_a", 0, 9, 11)
    femm.mi_clearselected()

    femm.mi_setcomment("round-trip fixture: every element type")
    femm.mi_zoomnatural()
    femm.mi_saveas(_save(path))


# ---------------------------------------------------------------------------
# 1. .fem semantic round-trip
# ---------------------------------------------------------------------------

def test_fem_semantic_roundtrip():
    """save -> reload -> save -> reload -> save must be stable.

    The comparison is between the SECOND and THIRD saves, not the first and
    second. Arc column 7 (mySideLength) is a derived rendering value that
    the text format does not actually round-trip: the writer emits it but
    CFemmeDoc::OnOpenDocument parses only seven arc fields and then sets
    mySideLength = MaxSideLength. A freshly drawn arc carries the
    constructor default of 1 (bd_nosebl.cpp), so the first save records 1
    and every save after a load records MaxSideLength. Comparing save 1
    against save 2 would flag that difference forever without it being data
    loss; comparing save 2 against save 3 asserts the thing that actually
    matters -- that a load/save cycle is a fixed point.
    """
    first = os.path.join(OUTPUT_DIR, "rich_first.fem")
    second = os.path.join(OUTPUT_DIR, "rich_second.fem")
    third = os.path.join(OUTPUT_DIR, "rich_third.fem")
    for p in (first, second, third):
        if os.path.exists(p):
            os.remove(p)

    build_rich_model(first)
    try:
        femm.mi_close()
        femm.opendocument(_save(first))
        femm.mi_saveas(_save(second))
        femm.mi_close()
        femm.opendocument(_save(second))
        femm.mi_saveas(_save(third))
    finally:
        _teardown()

    left, right = parse_fem(second), parse_fem(third)
    _note("    parsed %d scalars, %d property blocks, %d geometry tables"
          % (len(left["scalars"]),
             sum(len(v) for v in left["blocks"].values()),
             len(left["geometry"])))

    # every element type must actually be present, or the fixture is not
    # exercising what it claims to
    assert len(left["geometry"].get("NumPoints", [])) >= 8
    assert len(left["geometry"].get("NumSegments", [])) >= 4
    assert len(left["geometry"].get("NumArcSegments", [])) >= 4
    assert len(left["geometry"].get("NumBlockLabels", [])) >= 3
    assert len(left["blocks"]["block_props"]) >= 3
    assert len(left["blocks"]["circuit_props"]) >= 2
    assert len(left["blocks"]["bdry_props"]) >= 2
    assert len(left["blocks"]["point_props"]) >= 1
    assert any("_rows" in b for b in left["blocks"]["block_props"]), (
        "no BH-curve rows in the fixture: the nonlinear material did not "
        "round-trip its curve data")

    problems = diff_fem(left, right)
    assert not problems, ("%d field(s) drifted across a .fem load/save "
                          "cycle:%s%s"
                          % (len(problems), os.linesep,
                             os.linesep.join("  " + p for p in problems[:20])))


def test_fem_first_save_differs_only_in_derived_arc_column():
    """Pins the one known non-idempotent column, so it cannot grow quietly.

    If a future change makes the first save differ in anything MORE than arc
    column 7, this fails and names it.
    """
    first = os.path.join(OUTPUT_DIR, "rich_first.fem")
    second = os.path.join(OUTPUT_DIR, "rich_second.fem")
    assert os.path.exists(first) and os.path.exists(second), (
        "run test_fem_semantic_roundtrip first")

    problems = diff_fem(parse_fem(first), parse_fem(second))
    unexpected = [p for p in problems
                  if not p.startswith("NumArcSegments row")
                  or "column 7" not in p]
    _note("    first-vs-second save: %d difference(s), %d outside the known "
          "derived arc column" % (len(problems), len(unexpected)))
    assert not unexpected, (
        "the first save differs from the second in fields beyond the known "
        "derived arc column 7:%s%s"
        % (os.linesep, os.linesep.join("  " + p for p in unexpected[:20])))

# ---------------------------------------------------------------------------
# 2. .femx is written, and its freshness fields describe its source
# ---------------------------------------------------------------------------

def test_femx_cache_written_and_matches_source():
    """A .fem save refreshes .femx, whose header records the .fem's size/mtime."""
    path = os.path.join(OUTPUT_DIR, "cache_src.fem")
    femx = path[:-4] + ".femx"
    for p in (path, femx):
        if os.path.exists(p):
            os.remove(p)

    build_rich_model(path)
    try:
        pass
    finally:
        _teardown()

    assert os.path.exists(femx), (
        "saving %s did not produce a .femx cache alongside it" % path)

    hdr = read_femx_header(femx)
    assert hdr["magic"] == FEMX_MAGIC, "bad .femx magic: %r" % (hdr["magic"],)

    stat = os.stat(path)
    _note("    .femx v%d header=%dB records source size=%d mtime=%d; "
          ".fem on disk is size=%d mtime=%d"
          % (hdr["version"], hdr["header_size"], hdr["source_size"],
             hdr["source_mtime"], stat.st_size, int(stat.st_mtime)))
    assert hdr["source_size"] == stat.st_size, (
        "cache records source size %d but the .fem is %d bytes -- the cache "
        "would be treated as stale immediately"
        % (hdr["source_size"], stat.st_size))
    assert abs(hdr["source_mtime"] - int(stat.st_mtime)) <= 2, (
        "cache records source mtime %d but the .fem's is %d"
        % (hdr["source_mtime"], int(stat.st_mtime)))


# ---------------------------------------------------------------------------
# 3. Cache vs text must produce identical state
# ---------------------------------------------------------------------------

def test_femx_and_fem_produce_identical_state():
    """Opening through the cache and through the text parser must agree.

    Round-trips the same model twice: once with the .femx present (cache
    path) and once with it deleted (text path), then compares the two
    re-saves. FILE_FORMATS.md promises these are identical.
    """
    src = os.path.join(OUTPUT_DIR, "equiv_src.fem")
    femx = src[:-4] + ".femx"
    via_cache = os.path.join(OUTPUT_DIR, "equiv_via_cache.fem")
    via_text = os.path.join(OUTPUT_DIR, "equiv_via_text.fem")
    for p in (src, femx, via_cache, via_text):
        if os.path.exists(p):
            os.remove(p)

    build_rich_model(src)
    try:
        femm.mi_close()
    finally:
        _teardown()
    assert os.path.exists(femx)

    # cache path
    femm.openfemm(1)
    try:
        femm.opendocument(_save(src))
        femm.mi_saveas(_save(via_cache))
    finally:
        _teardown()

    # text path: same source, cache removed
    backup = femx + ".bak"
    shutil.move(femx, backup)
    femm.openfemm(1)
    try:
        femm.opendocument(_save(src))
        femm.mi_saveas(_save(via_text))
    finally:
        _teardown()

    problems = diff_fem(parse_fem(via_cache), parse_fem(via_text))
    _note("    cache-path vs text-path re-save: %d difference(s)" % len(problems))
    assert not problems, (
        "opening via .femx and via .fem produced different state:%s%s"
        % (os.linesep, os.linesep.join("  " + p for p in problems[:20])))


# ---------------------------------------------------------------------------
# 4. A stale cache must not be served
# ---------------------------------------------------------------------------

def test_femx_stale_cache_is_not_served():
    """A .fem edited after its .femx was written must be re-parsed as text.

    The cache is deliberately left describing the OLD file while the .fem
    is changed underneath it. If the stale cache were served, the reloaded
    model would still carry the original comment.
    """
    src = os.path.join(OUTPUT_DIR, "stale_src.fem")
    femx = src[:-4] + ".femx"
    out = os.path.join(OUTPUT_DIR, "stale_out.fem")
    for p in (src, femx, out):
        if os.path.exists(p):
            os.remove(p)

    build_rich_model(src)
    try:
        femm.mi_close()
    finally:
        _teardown()
    assert os.path.exists(femx)

    stale = femx + ".stale"
    shutil.copyfile(femx, stale)

    # edit the .fem behind the cache's back
    with open(src, "r", encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    marker = "STALE-CACHE-CANARY"
    assert "[Comment]" in text
    edited = []
    for line in text.split("\n"):
        if line.strip().startswith("[Comment]"):
            edited.append('[Comment]     =  "%s"' % marker)
        else:
            edited.append(line)
    time.sleep(1.1)   # ensure a distinct mtime second
    with open(src, "w", encoding="utf-8") as fh:
        fh.write("\n".join(edited))

    # put the OLD cache back, so it now describes a file that no longer exists
    shutil.copyfile(stale, femx)
    hdr = read_femx_header(femx)
    stat = os.stat(src)
    _note("    stale cache claims size=%d mtime=%d; .fem is now size=%d mtime=%d"
          % (hdr["source_size"], hdr["source_mtime"], stat.st_size,
             int(stat.st_mtime)))
    assert (hdr["source_size"] != stat.st_size
            or hdr["source_mtime"] != int(stat.st_mtime)), (
        "the edit did not change size or mtime, so this test cannot tell a "
        "stale cache from a fresh one")

    femm.openfemm(1)
    try:
        femm.opendocument(_save(src))
        femm.mi_saveas(_save(out))
    finally:
        _teardown()

    reloaded = parse_fem(out)
    comment = reloaded["scalars"].get("Comment", "")
    assert marker in comment, (
        "reloaded model's comment is %r, not the edited %r -- the stale "
        ".femx cache was served instead of re-parsing the .fem"
        % (comment, marker))


# ---------------------------------------------------------------------------
# 5. .ans / .ansx mesh round-trip
# ---------------------------------------------------------------------------

def test_ansx_cache_describes_its_ans():
    """Solving writes .ans; loading it caches .ansx describing that .ans."""
    src = os.path.join(OUTPUT_DIR, "solve_src.fem")
    ans = src[:-4] + ".ans"
    ansx = src[:-4] + ".ansx"
    for p in (src, ans, ansx):
        if os.path.exists(p):
            os.remove(p)

    build_rich_model(src)
    try:
        femm.mi_analyze(1)
        femm.mi_loadsolution()
        nodes = int(femm.mo_numnodes())
        elements = int(femm.mo_numelements())
    finally:
        _teardown()

    assert os.path.exists(ans), "analyze did not produce %s" % ans
    assert os.path.exists(ansx), (
        "loading the solution did not produce the .ansx mesh cache")

    hdr = read_ansx_header(ansx)
    stat = os.stat(ans)
    _note("    .ansx v%d records %d nodes / %d elements; post-processor "
          "reports %d / %d" % (hdr["version"], hdr["node_count"],
                               hdr["element_count"], nodes, elements))
    assert hdr["magic"] == ANSX_MAGIC, "bad .ansx magic: %r" % (hdr["magic"],)
    assert hdr["source_size"] == stat.st_size, (
        "cache records source size %d but the .ans is %d bytes"
        % (hdr["source_size"], stat.st_size))
    assert hdr["node_count"] == nodes, (
        "cache says %d nodes, the post-processor says %d"
        % (hdr["node_count"], nodes))
    assert hdr["element_count"] == elements, (
        "cache says %d elements, the post-processor says %d"
        % (hdr["element_count"], elements))


def test_ansx_solution_matches_between_cache_and_text():
    """Field values must be the same whether .ansx was used or not."""
    src = os.path.join(OUTPUT_DIR, "solve2_src.fem")
    ans = src[:-4] + ".ans"
    ansx = src[:-4] + ".ansx"
    for p in (src, ans, ansx):
        if os.path.exists(p):
            os.remove(p)

    build_rich_model(src)
    probes = ((5.0, 5.0), (16.0, 12.0), (32.0, 18.0), (20.0, 20.0))
    try:
        femm.mi_analyze(1)
        femm.mi_loadsolution()
        first = [femm.mo_getb(x, y) for x, y in probes]
        counts_first = (int(femm.mo_numnodes()), int(femm.mo_numelements()))
    finally:
        _teardown()
    assert os.path.exists(ansx)

    # text path: same .ans, cache removed
    os.remove(ansx)
    femm.openfemm(1)
    try:
        femm.opendocument(_save(src))
        femm.mi_loadsolution()
        second = [femm.mo_getb(x, y) for x, y in probes]
        counts_second = (int(femm.mo_numnodes()), int(femm.mo_numelements()))
    finally:
        _teardown()

    assert counts_first == counts_second, (
        "mesh size differs between the cached and text load: %r vs %r"
        % (counts_first, counts_second))

    worst = 0.0
    for (x, y), a, b in zip(probes, first, second):
        for ca, cb in zip(a, b):
            fa, fb = complex(ca), complex(cb)
            scale = max(abs(fa), abs(fb), 1e-30)
            worst = max(worst, abs(fa - fb) / scale)
    _note("    cached vs text .ans load: worst relative field difference %.3g"
          % worst)
    assert worst < 1e-9, (
        "B differs by %.3g between a cached (.ansx) and a text (.ans) load; "
        "FILE_FORMATS.md requires them to be identical" % worst)


def test_write_report():
    path = _write_report()
    assert os.path.exists(path)
    print("report: " + path)
