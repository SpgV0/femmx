"""
Material library and BH-curve IO (issue #9).

MaterialLibraryIO.cpp / BHCurve.h / MaterialLibraryDialog.cpp parse the
library every nonlinear magnetics model depends on. A parsing regression
there silently changes solved results rather than failing loudly, which
is the worst failure mode a test can be missing.

This runs against the libraries the installer actually ships (bin/*.dat),
so a bad edit to those files is caught too, and it parses them with an
INDEPENDENT Python reader -- a third opinion, not a reuse of the code
under test -- then cross-checks a sample against what FEMM itself loads
through mi_getmaterial.

Requirements: a built, COM-registered femmx.exe; pip install pyfemm pywin32.

Usage:
    pytest test/material_library_test.py -v
"""

import os

import pytest

import femm

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "results", "material_library")

# The libraries the installer ships (script.nsi installs bin/*.dat).
LIB_DIR = os.path.join(REPO_ROOT, "bin")
MATLIB = os.path.join(LIB_DIR, "matlib.dat")
HEATLIB = os.path.join(LIB_DIR, "heatlib.dat")
STATLIB = os.path.join(LIB_DIR, "statlib.dat")
CONDLIB = os.path.join(LIB_DIR, "condlib.dat")

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
    path = os.path.join(OUTPUT_DIR, "material_library.txt")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("Material library / BH curve checks" + os.linesep)
        fh.write("=" * 70 + os.linesep)
        for line in _REPORT:
            fh.write(line + os.linesep)
    return path


# ---------------------------------------------------------------------------
# An independent reader for the library format
# ---------------------------------------------------------------------------

def parse_library(path, begin="<BeginBlock>", end="<EndBlock>",
                  name_key="<BlockName>"):
    """[{name, props: {key: value}, curve: [(x, y)], line: int}]

    Deliberately a separate implementation from the C++ one under test.
    Folders are ignored: they are presentation only, and a material is
    identified by name regardless of where it sits in the tree.
    """
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        lines = [ln.rstrip("\n").rstrip("\r") for ln in fh]

    entries = []
    current = None
    for idx, raw in enumerate(lines):
        line = raw.strip()
        if line == begin:
            current = {"name": None, "props": {}, "curve": [], "line": idx + 1}
            continue
        if line == end:
            if current is not None:
                entries.append(current)
            current = None
            continue
        if current is None or not line:
            continue

        if line.startswith("<") and "=" in line:
            key, value = line.split("=", 1)
            key = key.strip()
            value = value.strip()
            if key == name_key:
                current["name"] = value.strip('"')
            else:
                current["props"][key] = value
            continue

        # a bare numeric row belongs to whatever curve the entry declares
        parts = line.split()
        if parts and all(_is_number(p) for p in parts):
            current["curve"].append(tuple(float(p) for p in parts))

    return entries


def _is_number(token):
    try:
        float(token)
        return True
    except ValueError:
        return False


def _num(entry, key, default=None):
    raw = entry["props"].get(key)
    if raw is None:
        return default
    try:
        return float(raw)
    except ValueError:
        return default


# ---------------------------------------------------------------------------
# 1. The shipped libraries all parse
# ---------------------------------------------------------------------------

# The shipped libraries organise materials into FOLDERS, and two entries in
# different folders are allowed to share a name. mi_getmaterial, though,
# looks materials up by NAME alone -- there is no way to say which folder --
# so a script asking for one of these silently gets whichever the parser
# reaches first, and the other is unreachable from scripting entirely. These
# are not near-duplicates: they are different substances.
#
#   matlib.dat  "Supermalloy"  Nickel Alloys           mu_r 529095, 12 BH pts
#                              Metals Handbook curves  mu_r 1,      34 BH pts
#   heatlib.dat "Ammonia"      Saturated Liquids       k = 0.546
#                              Gases at 1 atm          k = 0.0153  (36x apart)
#
# Pinned rather than "fixed": renaming entries in a library upstream ships,
# and that users' existing models reference by name, is not this test's call
# to make. Filed as issue #34. A NEW duplicate still fails.
KNOWN_DUPLICATES = {
    "matlib.dat": ("Supermalloy",),
    "heatlib.dat": ("Ammonia",),
}

LIBRARIES = [
    ("matlib.dat", MATLIB, "<BeginBlock>", "<EndBlock>", "<BlockName>", 200),
    ("heatlib.dat", HEATLIB, "<BeginBlock>", "<EndBlock>", "<BlockName>", 100),
    ("statlib.dat", STATLIB, "<BeginBlock>", "<EndBlock>", "<BlockName>", 20),
]


@pytest.mark.parametrize("label,path,begin,end,name_key,min_entries", LIBRARIES)
def test_shipped_library_parses(label, path, begin, end, name_key, min_entries):
    """Every entry in the shipped library must parse and be named."""
    assert os.path.exists(path), "shipped library missing: %s" % path
    entries = parse_library(path, begin, end, name_key)

    _note("    %-12s %d entries, %d with a curve"
          % (label, len(entries), sum(1 for e in entries if e["curve"])))
    assert len(entries) >= min_entries, (
        "%s parsed only %d entries, expected at least %d -- the file may be "
        "truncated or the format may have changed"
        % (label, len(entries), min_entries))

    unnamed = [e for e in entries if not e["name"]]
    assert not unnamed, (
        "%s has %d entry/entries with no name, first at line %d"
        % (label, len(unnamed), unnamed[0]["line"]))

    names = [e["name"] for e in entries]
    duplicates = sorted({n for n in names if names.count(n) > 1})
    unexpected = sorted(set(duplicates) - set(KNOWN_DUPLICATES.get(label, ())))
    if duplicates:
        _note("    %-12s duplicate names present: %s (known: %s)"
              % (label, duplicates, sorted(KNOWN_DUPLICATES.get(label, ()))))
    assert not unexpected, (
        "%s has NEW duplicate material names, which makes mi_getmaterial "
        "ambiguous: %s" % (label, unexpected))


def test_matlib_declared_curve_lengths_match_the_data():
    """<BHPoints> = N must be followed by exactly N points.

    A count that disagrees with the rows after it is precisely the kind of
    bad edit that changes solved results without any error: the reader
    trusts the count.
    """
    entries = parse_library(MATLIB)
    bad = []
    for e in entries:
        declared = int(_num(e, "<BHPoints>", 0) or 0)
        actual = len(e["curve"])
        if declared != actual:
            bad.append((e["name"], e["line"], declared, actual))

    nonlinear = [e for e in entries if _num(e, "<BHPoints>", 0)]
    _note("    matlib.dat: %d nonlinear materials, all BHPoints counts %s"
          % (len(nonlinear), "match" if not bad else "MISMATCHED"))
    assert not bad, (
        "materials whose <BHPoints> count disagrees with the rows that "
        "follow it: %s"
        % "; ".join("%s (line %d) declares %d, has %d" % b for b in bad[:10]))


def test_matlib_bh_curves_are_monotonic_and_start_at_origin():
    """A BH curve that is not monotonic makes the interpolation ambiguous."""
    entries = parse_library(MATLIB)
    problems = []
    checked = 0
    for e in entries:
        curve = e["curve"]
        if len(curve) < 2:
            continue
        checked += 1
        b0, h0 = curve[0][0], curve[0][1]
        if abs(b0) > 1e-12 or abs(h0) > 1e-12:
            problems.append("%s starts at (%g, %g), not the origin"
                            % (e["name"], b0, h0))
        for (b_prev, h_prev), (b, h) in zip(curve, curve[1:]):
            if b <= b_prev:
                problems.append("%s: B not increasing (%g -> %g)"
                                % (e["name"], b_prev, b))
                break
            if h < h_prev:
                problems.append("%s: H decreases (%g -> %g)"
                                % (e["name"], h_prev, h))
                break

    _note("    matlib.dat: %d BH curves checked for monotonicity" % checked)
    assert checked > 0, "no BH curves found to check"
    assert not problems, ("%d BH curve problem(s): %s"
                          % (len(problems), "; ".join(problems[:8])))


def test_spot_check_known_materials():
    """A few values pinned by hand, so a wholesale format shift is caught."""
    entries = {e["name"]: e for e in parse_library(MATLIB)}

    assert "Air" in entries, "the library has no Air"
    air = entries["Air"]
    assert _num(air, "<Mu_x>") == 1.0, "Air Mu_x is %r" % air["props"].get("<Mu_x>")
    assert _num(air, "<Mu_y>") == 1.0
    assert _num(air, "<Sigma>") == 0.0
    assert not air["curve"], "Air should be linear"

    copper = entries.get("Copper")
    assert copper is not None, "the library has no Copper"
    sigma = _num(copper, "<Sigma>")
    # FEMM stores conductivity in MS/m; copper is ~58
    assert 55.0 < sigma < 60.0, (
        "Copper conductivity is %r MS/m, expected about 58" % sigma)

    _note("    spot checks: Air mu_r=%g sigma=%g | Copper sigma=%g MS/m"
          % (_num(air, "<Mu_x>"), _num(air, "<Sigma>"), sigma))


# ---------------------------------------------------------------------------
# 2. What FEMM loads must match what the file says
# ---------------------------------------------------------------------------

def _saved_block_props(tag):
    """The <BeginBlock> records FEMM writes into a .fem."""
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    path = os.path.join(OUTPUT_DIR, "%s.fem" % tag)
    if os.path.exists(path):
        os.remove(path)
    femm.mi_saveas(_save(path))
    return {e["name"]: e for e in parse_library(path)}, path


SAMPLE_MATERIALS = ["Air", "Copper", "Alnico 5", "Pure Iron", "M-19 Steel"]


def test_library_values_survive_mi_getmaterial():
    """mi_getmaterial must reproduce the library's own numbers."""
    library = {e["name"]: e for e in parse_library(MATLIB)}
    wanted = [m for m in SAMPLE_MATERIALS if m in library]
    assert len(wanted) >= 3, (
        "expected at least three of %r in the library, found %r"
        % (SAMPLE_MATERIALS, wanted))

    femm.openfemm(1)
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-9, 1, 30)
    try:
        for name in wanted:
            femm.mi_getmaterial(name)
        loaded, _ = _saved_block_props("getmaterial")
    finally:
        _teardown()

    compared = 0
    for name in wanted:
        assert name in loaded, (
            "mi_getmaterial(%r) did not put the material into the model" % name)
        src, got = library[name], loaded[name]
        for key in ("<Mu_x>", "<Mu_y>", "<H_c>", "<Sigma>", "<LamType>",
                    "<LamFill>", "<NStrands>", "<WireD>"):
            if key not in src["props"]:
                continue
            a, b = _num(src, key), _num(got, key)
            if a is None or b is None:
                continue
            scale = max(abs(a), abs(b), 1.0)
            assert abs(a - b) / scale < 1e-9, (
                "%s %s: library says %r, FEMM loaded %r"
                % (name, key, src["props"][key], got["props"].get(key)))
            compared += 1

        if src["curve"]:
            assert len(got["curve"]) == len(src["curve"]), (
                "%s: library has %d BH points, FEMM loaded %d"
                % (name, len(src["curve"]), len(got["curve"])))
            for (b_src, h_src), (b_got, h_got) in zip(src["curve"], got["curve"]):
                assert abs(b_src - b_got) < 1e-9 and abs(h_src - h_got) < 1e-9, (
                    "%s: BH point (%g, %g) came back as (%g, %g)"
                    % (name, b_src, h_src, b_got, h_got))

    _note("    mi_getmaterial: %d materials, %d property values and every BH "
          "point identical to the library" % (len(wanted), compared))


def test_duplicate_name_resolves_to_the_first_entry():
    """Pins WHICH "Supermalloy" a script actually gets.

    The library has two, in different folders, with wildly different
    properties (mu_r 529095 with 12 BH points, versus mu_r 1 with 34).
    mi_getmaterial takes a name and no folder, so one of them is simply
    unreachable from scripting. Asserting which one wins turns a silent,
    surprising behaviour into a documented one -- and if the library is
    ever reordered, this fails and says the answer changed.
    """
    entries = [e for e in parse_library(MATLIB) if e["name"] == "Supermalloy"]
    if len(entries) < 2:
        pytest.skip("matlib.dat no longer has duplicate Supermalloy entries")

    femm.openfemm(1)
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-9, 1, 30)
    try:
        femm.mi_getmaterial("Supermalloy")
        loaded, _ = _saved_block_props("duplicate_name")
    finally:
        _teardown()

    got = loaded["Supermalloy"]
    got_mu = _num(got, "<Mu_x>")
    got_points = len(got["curve"])
    candidates = [(e["line"], _num(e, "<Mu_x>"), len(e["curve"])) for e in entries]
    _note("    duplicate resolution: mi_getmaterial('Supermalloy') -> "
          "mu_r=%g with %d BH points; candidates %s"
          % (got_mu, got_points, candidates))

    first = entries[0]
    assert abs(got_mu - _num(first, "<Mu_x>")) < 1e-6, (
        "mi_getmaterial returned mu_r=%g, but the FIRST Supermalloy in the "
        "file (line %d) has mu_r=%g. Which duplicate wins has changed."
        % (got_mu, first["line"], _num(first, "<Mu_x>")))
    assert got_points == len(first["curve"]), (
        "mi_getmaterial returned %d BH points, the first entry has %d"
        % (got_points, len(first["curve"])))

# ---------------------------------------------------------------------------
# 3. BH curves built by script
# ---------------------------------------------------------------------------

SCRIPTED_CURVE = [
    (0.0, 0.0),
    (0.25, 120.0),
    (0.60, 310.0),
    (1.00, 850.0),
    (1.40, 2600.0),
    (1.70, 11000.0),
    (1.90, 46000.0),
]


def test_scripted_bh_curve_round_trips():
    """mi_addbhpoint data must survive a save/reload unchanged."""
    first = os.path.join(OUTPUT_DIR, "scripted_bh.fem")
    second = os.path.join(OUTPUT_DIR, "scripted_bh_reload.fem")

    femm.openfemm(1)
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-9, 1, 30)
    try:
        femm.mi_addmaterial("scripted", 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0)
        for b, h in SCRIPTED_CURVE:
            femm.mi_addbhpoint("scripted", b, h)
        saved, _ = _saved_block_props("scripted_bh")
        femm.mi_close()
        femm.opendocument(_save(first))
        reloaded, _ = _saved_block_props("scripted_bh_reload")
    finally:
        _teardown()

    assert "scripted" in saved, "mi_addmaterial did not create the material"
    src = saved["scripted"]["curve"]
    got = reloaded["scripted"]["curve"]

    _note("    scripted BH curve: %d points written, %d after reload"
          % (len(src), len(got)))
    assert len(src) == len(SCRIPTED_CURVE), (
        "wrote %d BH points, the file has %d" % (len(SCRIPTED_CURVE), len(src)))
    assert len(got) == len(src), (
        "BH point count changed across a reload: %d -> %d" % (len(src), len(got)))
    for (b_want, h_want), (b_got, h_got) in zip(SCRIPTED_CURVE, got):
        assert abs(b_want - b_got) < 1e-9 and abs(h_want - h_got) < 1e-9, (
            "BH point (%g, %g) came back as (%g, %g)"
            % (b_want, h_want, b_got, h_got))


def test_clearbhpoints_empties_the_curve():
    """The inverse operation must actually remove the data."""
    femm.openfemm(1)
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-9, 1, 30)
    try:
        femm.mi_addmaterial("temp", 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0)
        for b, h in SCRIPTED_CURVE:
            femm.mi_addbhpoint("temp", b, h)
        with_points, _ = _saved_block_props("bh_before_clear")
        femm.mi_clearbhpoints("temp")
        cleared, _ = _saved_block_props("bh_after_clear")
    finally:
        _teardown()

    _note("    mi_clearbhpoints: %d points -> %d"
          % (len(with_points["temp"]["curve"]), len(cleared["temp"]["curve"])))
    assert len(with_points["temp"]["curve"]) == len(SCRIPTED_CURVE)
    assert not cleared["temp"]["curve"], (
        "mi_clearbhpoints left %d point(s) behind"
        % len(cleared["temp"]["curve"]))


# ---------------------------------------------------------------------------
# 4. Lamination and wire properties
# ---------------------------------------------------------------------------

def test_lamination_and_wire_properties_survive_save_load():
    """LamType/LamFill/NStrands/WireD must round-trip.

    mi_addmaterial is (name, mu_x, mu_y, H_c, J, Cduct, Lam_d, Phi_hmax,
    LamFill, LamType, Phi_hx, Phi_hy); the stranded-wire fields are set
    through mi_modifymaterial afterwards, which is the path a script
    building a wound component actually takes.
    """
    first = os.path.join(OUTPUT_DIR, "wire.fem")

    femm.openfemm(1)
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-9, 1, 30)
    try:
        # laminated core: LamFill 0.94, LamType 1 (laminated in-plane), d_lam 0.5
        femm.mi_addmaterial("lamcore", 2000, 2000, 0, 0, 0, 0.5, 0, 0.94, 1, 0, 0)
        # stranded winding: 40 strands of 0.2 mm wire, LamType 3 (magnet wire)
        femm.mi_addmaterial("winding", 1, 1, 0, 0, 58, 0, 0, 1, 3, 0, 0)
        femm.mi_modifymaterial("winding", 12, 40)     # NStrands
        femm.mi_modifymaterial("winding", 13, 0.2)    # WireD

        saved, _ = _saved_block_props("wire")
        femm.mi_close()
        femm.opendocument(_save(first))
        reloaded, _ = _saved_block_props("wire_reload")
    finally:
        _teardown()

    for name in ("lamcore", "winding"):
        assert name in saved and name in reloaded, "%s went missing" % name

    lam = reloaded["lamcore"]
    _note("    lamcore: LamType=%s LamFill=%s d_lam=%s"
          % (lam["props"].get("<LamType>"), lam["props"].get("<LamFill>"),
             lam["props"].get("<d_lam>")))
    assert _num(lam, "<LamType>") == 1.0, (
        "LamType came back as %r" % lam["props"].get("<LamType>"))
    assert abs(_num(lam, "<LamFill>") - 0.94) < 1e-9, (
        "LamFill came back as %r" % lam["props"].get("<LamFill>"))
    assert abs(_num(lam, "<d_lam>") - 0.5) < 1e-9, (
        "d_lam came back as %r" % lam["props"].get("<d_lam>"))

    wind = reloaded["winding"]
    _note("    winding: LamType=%s NStrands=%s WireD=%s"
          % (wind["props"].get("<LamType>"), wind["props"].get("<NStrands>"),
             wind["props"].get("<WireD>")))
    assert _num(wind, "<LamType>") == 3.0
    assert _num(wind, "<NStrands>") == 40.0, (
        "NStrands came back as %r" % wind["props"].get("<NStrands>"))
    assert abs(_num(wind, "<WireD>") - 0.2) < 1e-9, (
        "WireD came back as %r" % wind["props"].get("<WireD>"))

    # and every one of those must be unchanged from the first save
    for name in ("lamcore", "winding"):
        for key in ("<LamType>", "<LamFill>", "<d_lam>", "<NStrands>",
                    "<WireD>", "<Mu_x>", "<Sigma>"):
            a, b = _num(saved[name], key), _num(reloaded[name], key)
            if a is None or b is None:
                continue
            assert abs(a - b) < 1e-9, (
                "%s %s drifted across the reload: %r -> %r"
                % (name, key, a, b))


# ---------------------------------------------------------------------------
# 5. Malformed entries
# ---------------------------------------------------------------------------

def test_malformed_library_entries_are_detectable():
    """The independent reader must notice a broken entry.

    The shipped library is not modified: a deliberately broken copy is
    parsed instead, which is what proves the checks above would actually
    fire on a bad edit rather than passing vacuously.
    """
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    broken = os.path.join(OUTPUT_DIR, "broken_matlib.dat")
    lines = [
        "<BeginBlock>",
        '<BlockName> = "GoodOne"',
        "<Mu_x> = 1",
        "<Mu_y> = 1",
        "<BHPoints> = 0",
        "<EndBlock>",
        "",
        "<BeginBlock>",           # declares 3 points, supplies 2
        '<BlockName> = "BadCount"',
        "<Mu_x> = 1",
        "<BHPoints> = 3",
        "\t0\t0",
        "\t1.0\t500",
        "<EndBlock>",
        "",
        "<BeginBlock>",           # no name at all
        "<Mu_x> = 1",
        "<BHPoints> = 0",
        "<EndBlock>",
    ]
    with open(broken, "w", encoding="utf-8") as fh:
        fh.write(os.linesep.join(lines))

    entries = parse_library(broken)
    assert len(entries) == 3, "expected 3 entries, parsed %d" % len(entries)

    unnamed = [e for e in entries if not e["name"]]
    assert len(unnamed) == 1, "the unnamed entry was not detected"

    by_name = {e["name"]: e for e in entries if e["name"]}
    bad = by_name["BadCount"]
    declared = int(_num(bad, "<BHPoints>", 0))
    assert declared == 3 and len(bad["curve"]) == 2, (
        "the count/rows mismatch was not detected: declared %d, rows %d"
        % (declared, len(bad["curve"])))
    _note("    broken-library probe: unnamed entry and BHPoints mismatch both "
          "detected, so the shipped-library checks are not vacuous")


def test_write_report():
    path = _write_report()
    assert os.path.exists(path)
    print("report: " + path)
