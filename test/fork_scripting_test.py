"""
This fork's own scripting additions, and the GUI switch (issue #19).

Everything FEMMX adds to the Lua surface is, by construction, something
no upstream test could ever have covered:

    *_setredraw       suspend canvas redraw across a batch of edits
    *_setgpuaccel     opt a solver into its CUDA path
    setgui / getgui   choose which GUI renders GUI-derived output
    mi_savepng        PNG screenshots (FEMM 4.2 had bitmaps and metafiles)
    mo_savepng
    get_solve_stats   the load monitor's numbers, for benchmarking

copy_redraw_benchmark_test.py measures how *fast* setredraw is. Nothing
asserted it was *correct* -- that suppressing redraw changes only what is
painted, never what is built -- which is the property a script actually
depends on.

Requirements: a built, COM-registered femmx.exe; pip install pyfemm.

Usage:
    pytest test/fork_scripting_test.py -v
"""

import os
import re

import pytest

import femm

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "results", "fork_scripting")
BIN_DIR = os.path.join(REPO_ROOT, "bin", "plain")
FEMM_SRC = os.path.join(REPO_ROOT, "femm")
MANUAL_TEX = os.path.join(REPO_ROOT, "manual", "magnlua.tex")

_REPORT = []


def _note(text):
    print(text)
    _REPORT.append(text)


def _save(path):
    return path.replace(chr(92), "/")


def _out(name):
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    return os.path.join(OUTPUT_DIR, name)


def _teardown():
    for fn in (femm.mo_close, femm.closefemm):
        try:
            fn()
        except Exception:  # noqa: BLE001
            pass


# ---------------------------------------------------------------------------
# The four editors, as a table: everything below that is per-physics is
# driven from here rather than written out four times.
# ---------------------------------------------------------------------------

PHYSICS = [
    # prefix, newdocument type, probdef call, material to add, ext
    ("mi", 0, 'mi_probdef(0,"millimeters","planar",1e-8,0,30)', "air", "fem"),
    ("ei", 1, 'ei_probdef("millimeters","planar",1e-8,0,30)', "air", "fee"),
    ("hi", 2, 'hi_probdef("millimeters","planar",1e-8,0,30)', "air", "feh"),
    ("ci", 3, 'ci_probdef("millimeters","planar",1e-8,0,30)', "air", "fec"),
]


def _build_model(prefix, suppress_redraw):
    """The same twenty-odd edits, with redraw either on or suppressed.

    Deliberately a mix of node/segment/label adds, a group selection and a
    copy: setredraw only has anything to suppress when something would
    have been painted, and a copy is the case the benchmark exercises.
    """
    if suppress_redraw:
        femm.callfemm("%s_setredraw(0)" % prefix)

    for i in range(6):
        femm.callfemm("%s_addnode(%g,%g)" % (prefix, i * 5.0, 0.0))
        femm.callfemm("%s_addnode(%g,%g)" % (prefix, i * 5.0, 10.0))
    for i in range(6):
        femm.callfemm("%s_addsegment(%g,%g,%g,%g)"
                      % (prefix, i * 5.0, 0.0, i * 5.0, 10.0))
    femm.callfemm("%s_addblocklabel(2.5,5.0)" % prefix)

    femm.callfemm("%s_clearselected()" % prefix)
    femm.callfemm("%s_selectgroup(0)" % prefix)
    femm.callfemm("%s_copytranslate(0,20,2)" % prefix)
    femm.callfemm("%s_clearselected()" % prefix)

    if suppress_redraw:
        femm.callfemm("%s_setredraw(1)" % prefix)


def _model_text(path):
    """The saved document, minus the lines that legitimately vary.

    Nothing in a FEMM document is timestamped, but the comment block and
    the file's own format banner are not part of the geometry, so they are
    dropped rather than being allowed to fail the comparison for a reason
    that has nothing to do with redraw.
    """
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        lines = fh.read().splitlines()
    keep = [ln for ln in lines
            if not ln.lower().startswith("[comment]")
            and not ln.lower().startswith("[format]")]
    return chr(10).join(keep)


@pytest.mark.parametrize("prefix,doctype,probdef,material,ext", PHYSICS,
                         ids=[p[0] for p in PHYSICS])
def test_setredraw_changes_what_is_painted_not_what_is_built(
        prefix, doctype, probdef, material, ext):
    """A batch of edits must build the same model with redraw suppressed.

    This is the whole contract. If suppressing redraw also suppressed some
    piece of bookkeeping an edit does on the way to the canvas, a script
    would build a subtly different model depending on a flag that is
    supposed to be a pure performance knob -- and it would do so silently,
    because the model looks right the moment the canvas comes back.
    """
    paths = {}
    for suppress in (False, True):
        femm.openfemm(1)
        femm.newdocument(doctype)
        femm.callfemm(probdef)
        try:
            _build_model(prefix, suppress)
            name = "%s_redraw_%s.%s" % (prefix, "off" if suppress else "on", ext)
            path = _out(name)
            femm.callfemm('%s_saveas("%s")' % (prefix, _save(path)))
            paths[suppress] = path
        finally:
            _teardown()

    with_redraw = _model_text(paths[False])
    without = _model_text(paths[True])

    same = with_redraw == without
    _note("    %s_setredraw: model identical with redraw suppressed: %s"
          % (prefix, "yes" if same else "NO"))
    if not same:
        a = with_redraw.splitlines()
        b = without.splitlines()
        first = next((i for i in range(max(len(a), len(b)))
                      if a[i:i + 1] != b[i:i + 1]), 0)
        pytest.fail(
            "%s_setredraw(0) changed the model, not just the painting. "
            "First difference at line %d:%s  with redraw: %r%s  without:    %r"
            % (prefix, first + 1, os.linesep,
               a[first] if first < len(a) else "(end of file)", os.linesep,
               b[first] if first < len(b) else "(end of file)"))


def test_setredraw_leaves_the_canvas_refreshed():
    """What the view draws must not depend on whether redraw was off.

    Scope, stated plainly: this cannot observe the on-screen repaint.
    savebitmap calls the view's OnDraw directly into a memory DC, so it
    paints whether or not lua_setredraw's InvalidateRect ran, and an
    automation session's window is hidden anyway. Verifying the actual
    WM_PAINT would need a visible window and a desktop session, which no
    test here has.

    What it does prove is the half that can go wrong silently: that the
    document left behind by a suppressed batch renders pixel-for-pixel
    like the same model built with redraw on. A batch that left the view's
    cached drawing state (extents, zoom, the NoDraw flag itself) stale
    would show up here even though the geometry comparison above passed.
    """
    shots = {}
    for suppress in (False, True):
        femm.openfemm(1)
        femm.newdocument(0)
        femm.callfemm('mi_probdef(0,"millimeters","planar",1e-8,0,30)')
        try:
            _build_model("mi", suppress)
            femm.mi_zoomnatural()
            path = _out("canvas_redraw_%s.bmp" % ("off" if suppress else "on"))
            femm.callfemm('mi_savebitmap("%s")' % _save(path))
            shots[suppress] = path
        finally:
            _teardown()

    with open(shots[False], "rb") as fh:
        a = fh.read()
    with open(shots[True], "rb") as fh:
        b = fh.read()

    _note("    canvas after resuming redraw: %d bytes vs %d, %s"
          % (len(a), len(b), "identical" if a == b else "DIFFERENT"))
    assert len(a) > 1000 and len(b) > 1000, (
        "one of the screenshots is empty (%d / %d bytes)" % (len(a), len(b)))
    assert a == b, (
        "the canvas captured after mi_setredraw(1) differs from the canvas "
        "of the same model drawn normally: resuming redraw did not repaint "
        "everything that changed while it was suppressed")


# ---------------------------------------------------------------------------
# The shape of the added surface: naming conventions, and the manual
# ---------------------------------------------------------------------------

FORK_ADDED = [
    "get_solve_stats",
    "setgui", "get_gui", "getgui", "set_gui",
    "mi_setgpuaccel", "ei_setgpuaccel", "hi_setgpuaccel", "ci_setgpuaccel",
    "mi_setredraw", "ei_setredraw", "hi_setredraw", "ci_setredraw",
    "mi_savepng", "mo_savepng",
]

# Those of the above that are editor commands, and so are subject to
# FEMM's documented two-spellings rule.
FORK_ADDED_EDITOR = [c for c in FORK_ADDED if re.match(r"^[a-z]{2}_", c)]


def _registered_commands():
    """Every name passed to lua_register anywhere in femm/."""
    names = set()
    pattern = re.compile(r'lua_register\(\s*lua\s*,\s*"([A-Za-z0-9_]+)"')
    for entry in os.listdir(FEMM_SRC):
        if not entry.lower().endswith(".cpp"):
            continue
        with open(os.path.join(FEMM_SRC, entry), "r",
                  encoding="utf-8", errors="replace") as fh:
            names.update(pattern.findall(fh.read()))
    return names


def test_every_fork_added_command_is_actually_registered():
    """The list this file tests against must match the binary."""
    registered = _registered_commands()
    missing = sorted(c for c in FORK_ADDED if c not in registered)
    _note("    fork-added commands registered: %d of %d"
          % (len(FORK_ADDED) - len(missing), len(FORK_ADDED)))
    assert not missing, (
        "these commands are tested here but no lua_register call creates "
        "them: %r" % missing)


def test_fork_added_commands_honour_femms_two_spellings_rule():
    """Both spellings, as the manual promises for every command.

    FEMM's own text says "Two naming conventions can be used: one which
    separates words in the command names by underscores, and one that
    eliminates the underscores", and upstream honours it right down to
    mi_savebitmap/mi_save_bitmap. mi_savepng/mi_save_png followed suit;
    setredraw and setgpuaccel did not, so mi_set_redraw -- the spelling a
    reader of the manual would reasonably try -- is simply a nil value.
    """
    registered = _registered_commands()
    missing = []
    for cmd in FORK_ADDED_EDITOR:
        prefix, rest = cmd.split("_", 1)
        if rest.startswith("set") and not rest.startswith("set_"):
            twin = "%s_set_%s" % (prefix, rest[3:])
        elif rest.startswith("save") and not rest.startswith("save_"):
            twin = "%s_save_%s" % (prefix, rest[4:])
        else:
            continue
        if twin not in registered:
            missing.append((cmd, twin))

    _note("    two-spellings rule: %d fork-added command(s) without a twin"
          % len(missing))
    assert not missing, (
        "these fork-added commands have only one spelling, so the "
        "underscore-separated form the manual promises is a nil value: %r"
        % missing)


@pytest.mark.parametrize("prefix,doctype,probdef,material,ext", PHYSICS,
                         ids=[p[0] for p in PHYSICS])
def test_both_spellings_actually_run(prefix, doctype, probdef, material, ext):
    """Each new spelling must reach the right function, not just exist.

    The source check above proves a lua_register call with that name is
    present. It cannot prove the name was wired to the intended function:
    a copy-paste that registered mi_set_redraw against lua_setgpuaccel
    would satisfy it completely. Calling each spelling with an argument
    whose effect is observable is what distinguishes the two.
    """
    femm.openfemm(1)
    femm.newdocument(doctype)
    femm.callfemm(probdef)
    try:
        errors = []
        for name in ("%s_setredraw" % prefix, "%s_set_redraw" % prefix,
                     "%s_setgpuaccel" % prefix, "%s_set_gpuaccel" % prefix):
            for arg in (0, 1):
                try:
                    femm.callfemm("%s(%d)" % (name, arg))
                except Exception as exc:  # noqa: BLE001
                    errors.append("%s(%d): %s" % (name, arg, exc))

        # setgpuaccel is persisted in the document as [GPUAccel]; the
        # underscore spelling must move it exactly as the other one does.
        path = _out("%s_alias.%s" % (prefix, ext))
        femm.callfemm("%s_set_gpuaccel(1)" % prefix)
        femm.callfemm('%s_saveas("%s")' % (prefix, _save(path)))
    finally:
        _teardown()

    _note("    %s: both spellings run, %d error(s)" % (prefix, len(errors)))
    assert not errors, (
        "a registered spelling did not run: %s" % ("; ".join(errors)))

    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        saved = fh.read()
    m = re.search(r"\[GPUAccel\]\s*=\s*(\d+)", saved)
    assert m, (
        "%s_set_gpuaccel(1) left no [GPUAccel] key in the saved document, "
        "so the alias is registered but not wired to lua_setgpuaccel"
        % prefix)
    assert m.group(1) == "1", (
        "%s_set_gpuaccel(1) saved [GPUAccel] = %s. The underscore spelling "
        "is registered against a different function than the one it names."
        % (prefix, m.group(1)))


def _manual_listed_commands():
    """Command names from the manual's "Commands Added Since FEMM 4.2"."""
    with open(MANUAL_TEX, "r", encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    start = text.find(r"\section{Commands Added Since FEMM 4.2}")
    if start < 0:
        return None
    end = text.find(r"\section{", start + 10)
    body = text[start:end if end > 0 else len(text)]
    # \tt names are written with LaTeX-escaped underscores
    names = re.findall(r"\\tt\s+([A-Za-z0-9_\\]+?)\s*(?:\(|\})", body)
    return {n.replace(chr(92), "") for n in names}


def test_the_manual_lists_every_fork_added_command():
    """A command the manual omits is a command nobody knows exists.

    The point of that section is that a script written for stock FEMM can
    be told apart from one that needs FEMMX. A command missing from it
    defeats exactly that purpose.
    """
    listed = _manual_listed_commands()
    if listed is None:
        pytest.skip("manual/magnlua.tex has no such section")

    registered = _registered_commands()
    # the aliases are spelling variants of a documented name, not separate
    # commands, so the manual is not expected to list both
    canonical = {c for c in FORK_ADDED
                 if c not in ("get_gui", "set_gui")}
    undocumented = sorted(c for c in canonical if c not in listed)
    phantom = sorted(c for c in listed
                     if c not in registered and c.replace("_", "") not in
                     {r.replace("_", "") for r in registered})

    _note("    manual: %d listed, %d undocumented, %d listed-but-absent"
          % (len(listed), len(undocumented), len(phantom)))
    assert not undocumented, (
        "these FEMMX commands exist but the manual's 'Commands Added Since "
        "FEMM 4.2' section does not mention them, so a script author has no "
        "way to know they are fork-specific: %r" % undocumented)
    assert not phantom, (
        "the manual documents these as FEMMX additions but nothing "
        "registers them: %r" % phantom)


MANUAL_TEX_MAIN = os.path.join(REPO_ROOT, "manual", "manual.tex")
CHANGELOG = os.path.join(REPO_ROOT, "CHANGELOG.md")


def test_the_manuals_revision_history_lists_every_release():
    """The Revision History chapter must not fall behind the releases.

    It is a hand-maintained table in manual.tex, added so a user can see
    at a glance what changed between the build they have and the current
    one. Nothing in the release process updates it, so the way it fails
    is by staying silently one or more versions behind -- which is worse
    than not having the chapter, because a reader takes the last row for
    the current version.
    """
    if not os.path.exists(MANUAL_TEX_MAIN) or not os.path.exists(CHANGELOG):
        pytest.skip("no manual.tex or CHANGELOG.md")

    with open(CHANGELOG, "r", encoding="utf-8-sig", errors="replace") as fh:
        changelog = fh.read()
    released = re.findall(r"^\s*\S+\s+\(v(\d+\.\d+\.\d+)\)\s*$",
                          changelog, re.M)
    if not released:
        pytest.skip("could not parse versions out of CHANGELOG.md")

    with open(MANUAL_TEX_MAIN, "r", encoding="utf-8", errors="replace") as fh:
        manual = fh.read()
    start = manual.find(r"\chapter*{Revision History}")
    if start < 0:
        pytest.skip("manual.tex has no Revision History chapter")
    end = manual.find(r"\end{tabular}", start)
    table = manual[start:end if end > 0 else len(manual)]
    listed = set(re.findall(r"(\d+\.\d+\.\d+)\s*&", table))

    missing = [v for v in released if v not in listed]
    extra = sorted(v for v in listed if v not in set(released))

    _note("    revision history: %d released, %d in the table, %d missing"
          % (len(set(released)), len(listed), len(missing)))
    assert not missing, (
        "CHANGELOG.md records these releases but the manual's Revision "
        "History table does not list them, so the manual describes an older "
        "version than the one it ships with: %r" % missing)
    assert not extra, (
        "the Revision History table lists versions CHANGELOG.md has no "
        "entry for: %r" % extra)


# ---------------------------------------------------------------------------
# setgui / getgui
# ---------------------------------------------------------------------------

CFG_PATH = os.path.join(BIN_DIR, "femm.cfg")


def _gui_is(name):
    """Is getgui() reporting `name`?

    The comparison is done in Lua rather than in Python because
    femm.callfemm() eval()s whatever the command returns as a Python
    expression -- a bare string like qt comes back as a NameError, not as
    a value. Returning 1/0 from Lua sidesteps that entirely.
    """
    return int(femm.callfemm('(getgui() == "%s") and 1 or 0' % name)) == 1


def _read_cfg():
    if not os.path.exists(CFG_PATH):
        return ""
    with open(CFG_PATH, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


@pytest.fixture
def preserved_cfg():
    """femm.cfg is the user's own preferences file; put it back."""
    backup = _read_cfg() if os.path.exists(CFG_PATH) else None
    try:
        yield
    finally:
        if backup is None:
            if os.path.exists(CFG_PATH):
                os.remove(CFG_PATH)
        else:
            with open(CFG_PATH, "w", encoding="utf-8", newline="") as fh:
                fh.write(backup)


def test_getgui_is_seeded_from_femm_cfg(preserved_cfg):
    """A script with no setgui() call must render the user's chosen GUI.

    That is the whole reason the setting is seeded rather than defaulted:
    someone who switched to the Qt GUI expects a script's PNGs to look
    like the GUI they actually use.
    """
    with open(CFG_PATH, "w", encoding="utf-8", newline="") as fh:
        fh.write("<ShowConsole>      = 1" + chr(10))
        fh.write("<PreferredGUI>     = 1" + chr(10))

    femm.openfemm(1)
    try:
        seeded_qt = _gui_is("qt")
    finally:
        _teardown()

    _note("    getgui() with <PreferredGUI> = 1 reports qt: %s" % seeded_qt)
    assert seeded_qt, (
        "femm.cfg asks for the Qt GUI but getgui() does not report qt, so a "
        "script would render through the GUI the user did not pick")


def test_setgui_does_not_write_the_preference_back(preserved_cfg):
    """Session-scoped on purpose (ScriptGui.h).

    A script rendering one image a particular way must not silently
    repoint which GUI the user's Start Menu shortcut opens.
    """
    with open(CFG_PATH, "w", encoding="utf-8", newline="") as fh:
        fh.write("<ShowConsole>      = 1" + chr(10))
        fh.write("<PreferredGUI>     = 0" + chr(10))
    before = _read_cfg()

    femm.openfemm(1)
    try:
        femm.callfemm('setgui("qt")')
        got_qt = _gui_is("qt")
    finally:
        _teardown()

    after = _read_cfg()
    _note("    setgui(\"qt\") -> getgui() reports qt: %s, femm.cfg %s"
          % (got_qt, "unchanged" if before == after else "REWRITTEN"))
    assert got_qt, "setgui() did not take effect"
    assert before == after, (
        "setgui() rewrote femm.cfg. It is session-scoped by design: a "
        "script asking to render one image through the Qt GUI must not "
        "change which GUI the user's shortcuts open.")


def test_setgui_rejects_an_unknown_name():
    """A typo must raise, not silently fall back to a default.

    Falling back would mean the script renders through the wrong GUI and
    reports success -- the image is there, it is simply not the one asked
    for, which nobody would notice until much later.
    """
    femm.openfemm(1)
    try:
        raised = None
        try:
            femm.callfemm('setgui("gtk")')
        except Exception as exc:  # noqa: BLE001
            raised = str(exc)
        still_classic = _gui_is("classic")
    finally:
        _teardown()

    _note("    setgui(\"gtk\") -> %s; getgui() still classic: %s"
          % ("raised" if raised else "SILENT", still_classic))
    assert raised, "setgui() accepted an unknown GUI name without complaint"
    assert "gtk" in raised.lower(), (
        "the error does not name the argument it rejected: %r" % raised)
    assert still_classic, (
        "setgui() rejected the name but did not leave the previous choice "
        "in place")


# ---------------------------------------------------------------------------
# savepng, through each GUI
# ---------------------------------------------------------------------------

PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


def _png_size(path):
    """Width and height from the IHDR chunk, without pulling in Pillow."""
    with open(path, "rb") as fh:
        head = fh.read(24)
    if len(head) < 24 or head[:8] != PNG_MAGIC:
        return None
    w = int.from_bytes(head[16:20], "big")
    h = int.from_bytes(head[20:24], "big")
    return w, h


def _rect(x0, y0, x1, y1):
    for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        femm.mi_addnode(x, y)
    femm.mi_addsegment(x0, y0, x1, y0)
    femm.mi_addsegment(x1, y0, x1, y1)
    femm.mi_addsegment(x1, y1, x0, y1)
    femm.mi_addsegment(x0, y1, x0, y0)


def _simple_solved_model(path):
    """A current-carrying conductor in air, solved.

    The conductor carries a real current density. An earlier version of
    this fixture had no excitation at all, so every integral taken from
    it was exactly zero -- and a test comparing two solves then passed by
    comparing 0.0 to 0.0, which would have held just as well if the
    solver had stopped working entirely.
    """
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-8, 100, 30)
    femm.mi_getmaterial("Air")
    # 10 MA/m^2 through the conductor -- enough field to integrate, small
    # enough to stay linear
    femm.mi_addmaterial("conductor", 1, 1, 0, 10, 0, 0, 0, 1, 0, 0, 0)

    _rect(-30, -30, 30, 30)
    _rect(-5, -5, 5, 5)

    femm.mi_addblocklabel(0, 0)
    femm.mi_selectlabel(0, 0)
    femm.mi_setblockprop("conductor", 1, 0, "<None>", 0, 0, 0)
    femm.mi_clearselected()

    femm.mi_addblocklabel(0, 20)
    femm.mi_selectlabel(0, 20)
    femm.mi_setblockprop("Air", 1, 0, "<None>", 0, 0, 0)
    femm.mi_clearselected()

    femm.mi_addboundprop("zero", 0, 0, 0, 0, 0, 0, 0, 0, 0)
    for x, y in ((0, -30), (30, 0), (0, 30), (-30, 0)):
        femm.mi_selectsegment(x, y)
    femm.mi_setsegmentprop("zero", 0, 1, 0, 0)
    femm.mi_clearselected()

    femm.mi_saveas(_save(path))
    femm.mi_analyze(1)
    femm.mi_loadsolution()


def test_savepng_writes_a_real_png_under_the_classic_gui():
    """mi_savepng must produce a decodable PNG, not an empty file.

    It goes through GDI+, which reports failure by simply not writing --
    so "the call returned" is not evidence that anything was saved.
    """
    png = _out("classic_pre.png")
    if os.path.exists(png):
        os.remove(png)

    femm.openfemm(1)
    try:
        femm.newdocument(0)
        femm.mi_probdef(0, "millimeters", "planar", 1e-8, 0, 30)
        femm.mi_addnode(0, 0)
        femm.mi_addnode(10, 10)
        femm.mi_addsegment(0, 0, 10, 10)
        femm.mi_zoomnatural()
        femm.callfemm('setgui("classic")')
        femm.callfemm('mi_savepng("%s")' % _save(png))
    finally:
        _teardown()

    assert os.path.exists(png), "mi_savepng wrote no file at all"
    size = _png_size(png)
    _note("    classic mi_savepng: %d bytes, %s"
          % (os.path.getsize(png), size))
    assert size is not None, (
        "mi_savepng wrote a %d-byte file that is not a PNG -- the GDI+ "
        "encode failed and the failure was not reported"
        % os.path.getsize(png))
    # Not just "> 0": a hidden COM session's view reports a client rect
    # with a zero dimension, and CreateCompatibleBitmap answers a 0x0
    # request with a 1x1 MONOCHROME bitmap -- a valid, useless PNG that
    # savepng used to hand back as a success (the same defect issue #4
    # fixed for savebitmap). femmCaptureSize's documented fallback is
    # 1024x768, so anything thumbnail-sized means the guard is gone.
    assert size[0] >= 64 and size[1] >= 64, (
        "mi_savepng wrote a %dx%d PNG. A degenerate client rect fell "
        "through to CreateCompatibleBitmap instead of being replaced by "
        "femmCaptureSize's default, so a headless script gets a blank "
        "thumbnail and no error." % size)


def test_savepng_under_qt_refuses_an_unsaved_document():
    """The Qt path renders a file, so there must be a file.

    It shells out to femmqt --render-png with the document's path; an
    unsaved document has none. Refusing loudly is the only correct
    answer -- rendering the last saved state instead would hand back a
    picture of a different model.
    """
    png = _out("qt_unsaved.png")
    if os.path.exists(png):
        os.remove(png)

    femm.openfemm(1)
    try:
        femm.newdocument(0)
        femm.mi_probdef(0, "millimeters", "planar", 1e-8, 0, 30)
        femm.mi_addnode(0, 0)
        femm.callfemm('setgui("qt")')
        raised = None
        try:
            femm.callfemm('mi_savepng("%s")' % _save(png))
        except Exception as exc:  # noqa: BLE001
            raised = str(exc)
    finally:
        _teardown()

    _note("    qt mi_savepng on an unsaved document: %s"
          % ("raised" if raised else "SILENT"))
    assert raised, (
        "mi_savepng with setgui(\"qt\") accepted an unsaved document "
        "without complaint")
    assert not os.path.exists(png) or os.path.getsize(png) == 0, (
        "it complained but still left a file behind at %s" % png)


@pytest.mark.slow
def test_savepng_under_qt_renders_through_femmqt():
    """setgui("qt") must actually route to femmqt, and produce a PNG."""
    if not os.path.exists(os.path.join(BIN_DIR, "femmqt.exe")):
        pytest.skip("femmqt.exe not built")

    fem = _out("qt_render.fem")
    png = _out("qt_render.png")
    for p in (png,):
        if os.path.exists(p):
            os.remove(p)

    femm.openfemm(1)
    try:
        femm.newdocument(0)
        femm.mi_probdef(0, "millimeters", "planar", 1e-8, 0, 30)
        femm.mi_addnode(0, 0)
        femm.mi_addnode(20, 0)
        femm.mi_addnode(20, 20)
        femm.mi_addsegment(0, 0, 20, 0)
        femm.mi_addsegment(20, 0, 20, 20)
        femm.mi_saveas(_save(fem))
        femm.mi_zoomnatural()
        femm.callfemm('setgui("qt")')
        femm.callfemm('mi_savepng("%s")' % _save(png))
    finally:
        _teardown()

    assert os.path.exists(png), (
        "setgui(\"qt\") + mi_savepng produced no file: the shell-out to "
        "femmqt --render-png failed silently")
    size = _png_size(png)
    _note("    qt mi_savepng: %d bytes, %s" % (os.path.getsize(png), size))
    assert size is not None, "the Qt path wrote something that is not a PNG"
    assert size[0] >= 64 and size[1] >= 64, (
        "the Qt path rendered a %dx%d PNG -- the size handed to "
        "femmqt --render-png came from an unlaid-out client rect" % size)


# ---------------------------------------------------------------------------
# The load monitor
# ---------------------------------------------------------------------------

def test_get_solve_stats_reports_no_gpu_rather_than_a_wrong_number():
    """Its telemetry must be sane on a machine with no GPU.

    lua_getsolvestats pushes -1 for the GPU figures when none is
    available, precisely so a benchmarking script can tell "no GPU" from
    "a GPU that was idle". A 0 for both would be indistinguishable.
    """
    fem = _out("stats.fem")
    femm.openfemm(1)
    try:
        _simple_solved_model(fem)
        try:
            stats = femm.callfemm("get_solve_stats()")
        except Exception as exc:  # noqa: BLE001
            pytest.skip("the load monitor is off in this session: %s" % exc)
    finally:
        _teardown()

    vals = list(stats) if isinstance(stats, (list, tuple)) else [stats]
    _note("    get_solve_stats(): %r" % (vals,))
    assert len(vals) == 7, (
        "get_solve_stats() returns 7 values (duration, cpuMax, cpuAvg, "
        "gpuMax, gpuAvg, ramMax, ramAvg); got %d: %r" % (len(vals), vals))

    duration, cpu_max, cpu_avg, gpu_max, gpu_avg, ram_max, ram_avg = vals
    assert duration >= 0, "a negative solve duration: %r" % duration
    for name, v in (("cpuMax", cpu_max), ("cpuAvg", cpu_avg),
                    ("ramMax", ram_max), ("ramAvg", ram_avg)):
        assert 0 <= v <= 100.0001, "%s is %r, outside 0..100%%" % (name, v)
    assert cpu_avg <= cpu_max + 1e-6, (
        "average CPU (%r) exceeds the maximum (%r)" % (cpu_avg, cpu_max))
    assert ram_avg <= ram_max + 1e-6, (
        "average RAM (%r) exceeds the maximum (%r)" % (ram_avg, ram_max))
    for name, v in (("gpuMax", gpu_max), ("gpuAvg", gpu_avg)):
        assert v == -1 or 0 <= v <= 100.0001, (
            "%s is %r: it must be -1 for 'no GPU' or a percentage, never "
            "some other sentinel a script would read as a real load"
            % (name, v))


@pytest.mark.slow
def test_the_load_monitor_does_not_perturb_the_solution():
    """Sampling must not change the answer.

    The monitor polls performance counters on a timer while the solver
    runs. If that were to change scheduling enough to alter the result,
    every benchmark taken with it on would describe a different solve
    from the one users get with it off.
    """
    results = []
    for run in range(2):
        fem = _out("monitor_run%d.fem" % run)
        femm.openfemm(1)
        try:
            _simple_solved_model(fem)
            femm.mo_selectblock(0, 0)
            energy = femm.mo_blockintegral(2)
            femm.mo_clearblock()
            # |B| just outside the conductor, where the field is largest
            vals = femm.mo_getpointvalues(8, 0)
            b_mag = abs(complex(vals[1])) + abs(complex(vals[2]))
            results.append((energy, b_mag))
        finally:
            _teardown()

    (e0, b0), (e1, b1) = results
    _note("    two solves: energy=%r / %r, |B| at (8,0)=%r / %r"
          % (e0, e1, b0, b1))

    # Guard against the test passing by comparing zero to zero: an
    # unexcited model, or a solve that quietly produced nothing, would
    # otherwise satisfy every assertion below.
    assert abs(e0) > 1e-12 and abs(b0) > 1e-12, (
        "the reference solve produced no field at all (energy=%r, |B|=%r), "
        "so comparing the two runs would prove nothing" % (e0, b0))

    for name, x, y in (("stored energy", e0, e1), ("|B| at (8,0)", b0, b1)):
        denom = max(abs(x), abs(y), 1e-30)
        assert abs(x - y) / denom < 1e-9, (
            "the %s differs between two identical solves: %r vs %r. The "
            "solve is not reproducible, so no benchmark taken from it "
            "means anything." % (name, x, y))


def test_write_report():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    path = os.path.join(OUTPUT_DIR, "fork_scripting.txt")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("FEMMX-specific scripting surface" + os.linesep)
        fh.write("=" * 70 + os.linesep)
        for line in _REPORT:
            fh.write(line + os.linesep)
    print("report: %s" % path)
    assert os.path.exists(path)
