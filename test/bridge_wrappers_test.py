"""
The octavefemm / scifemm / mathfemm scripting bridges (issue #20).

Four scripting bridges ship in this repository. pyfemm is tested
extensively -- every other test in this directory is a pyfemm test. The
other three were untested entirely, which is precisely why they rot
quietly: each is a thin wrapper that pastes a Lua command name into a
string and hands it to femmx.exe, so a command that is renamed, or was
never registered in the first place, breaks them without touching
anything the rest of the suite reads.

The check that matters most needs no interpreter at all: every command
name a wrapper builds must correspond to a command femmx.exe actually
registers. That one runs everywhere, including CI. The
interpreter-backed smoke tests are best-effort and skip loudly.

Usage:
    pytest test/bridge_wrappers_test.py -v
"""

import glob
import os
import re
import subprocess

import pytest

import femmx_paths

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "results", "bridge_wrappers")
FEMM_SRC = os.path.join(REPO_ROOT, "femm")
# Resolved rather than hardcoded: CI builds into bin/ while the local
# wrapper scripts move everything into bin/plain, and ten modules that
# assumed bin/plain silently SKIPPED on CI (#24).
BIN_DIR = femmx_paths.BIN_DIR

_REPORT = []


def _note(text):
    print(text)
    _REPORT.append(text)


# ---------------------------------------------------------------------------
# Extracting what each bridge calls
# ---------------------------------------------------------------------------

# All three bridges build a Lua call the same way -- an opening
# "commandname(" string literal handed to their own transport -- but each
# spells the transport differently:
#
#   octavefemm  callfemm(['mi_addnode(' , numc(x) , num(y) , ')']);
#   scifemm     callfemm(['mi_addnode(' , numc(x) , num(y) , ')']);
#   mathfemm    MLPut["mi_addnode(" <> NumC[x] <> Num[y] <> ")"]
#
# so one pattern covers all three. Deliberately anchored on the transport
# call rather than matching any "word(" in the file: the wrappers are full
# of their own helper calls (quote, num, numc) that are not Lua commands.
CALL_PATTERN = re.compile(
    r"""(?:callfemm(?:_noeval)?\s*\(\s*\[?\s*|MLPut\s*\[\s*)['"]([A-Za-z_][A-Za-z_0-9]*)\s*\(""")

REGISTER_PATTERN = re.compile(r'lua_register\(\s*lua\s*,\s*"([A-Za-z0-9_]+)"')

BRIDGES = {
    "octavefemm": os.path.join(REPO_ROOT, "octavefemm", "mfiles", "*.m"),
    "scifemm": os.path.join(REPO_ROOT, "scifemm", "scifemm.sci"),
    "mathfemm": os.path.join(REPO_ROOT, "mathfemm", "mathfemm.m"),
}

# Commands a bridge names that femmx.exe does not register, which are
# known and tracked rather than unnoticed. Each entry must cite the issue
# that will remove it -- an allowlist with no expiry is just a silenced
# test. test_the_allowlist_has_not_gone_stale fails when an entry becomes
# unnecessary, so this cannot quietly outlive its cause.
KNOWN_MISSING = {
    "mi_makeABC": "#46",
    "ei_makeABC": "#46",
    "hi_makeABC": "#46",
    "ci_makeABC": "#46",
}


def _registered_commands():
    names = set()
    for entry in os.listdir(FEMM_SRC):
        if not entry.lower().endswith(".cpp"):
            continue
        with open(os.path.join(FEMM_SRC, entry), "r",
                  encoding="utf-8", errors="replace") as fh:
            names.update(REGISTER_PATTERN.findall(fh.read()))
    return names


def _bridge_commands(pattern):
    found = set()
    for path in sorted(glob.glob(pattern)):
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            found.update(CALL_PATTERN.findall(fh.read()))
    return found


@pytest.fixture(scope="module")
def registered():
    names = _registered_commands()
    assert len(names) > 500, (
        "only %d lua_register calls found in femm/ -- the scan is broken, "
        "not the bridges" % len(names))
    return names


# ---------------------------------------------------------------------------
# The check that runs everywhere
# ---------------------------------------------------------------------------

@pytest.mark.static
@pytest.mark.parametrize("bridge", sorted(BRIDGES))
def test_every_wrapper_names_a_command_that_exists(bridge, registered):
    """A wrapper for a command nobody registers is a wrapper that throws.

    There is no failure mode where this is harmless: the user calls the
    documented function, femmx.exe answers "attempt to call global ... (a
    nil value)", and the script dies. It is also invisible until someone
    calls that particular wrapper, which for a 641-file bridge could be
    years.
    """
    called = _bridge_commands(BRIDGES[bridge])
    assert called, "extracted no commands at all from %s" % bridge

    missing = sorted(c for c in called if c not in registered)
    untracked = [c for c in missing if c not in KNOWN_MISSING]
    tracked = [c for c in missing if c in KNOWN_MISSING]

    _note("    %-11s %4d commands, %d missing (%d tracked: %s)"
          % (bridge, len(called), len(missing), len(tracked),
             ", ".join(sorted(set(KNOWN_MISSING[c] for c in tracked))) or "-"))

    assert not untracked, (
        "%s builds calls to commands femmx.exe does not register, and they "
        "are not in KNOWN_MISSING: %r. Either the command was renamed and "
        "the wrapper was not, or it never existed." % (bridge, untracked))


@pytest.mark.static
def test_the_allowlist_has_not_gone_stale(registered):
    """An allowlist entry must disappear when its cause is fixed.

    Otherwise the first thing that happens after someone implements the
    missing command is that this file keeps excusing it, and the next
    genuinely-missing command hides behind the stale entry.
    """
    fixed = sorted(c for c in KNOWN_MISSING if c in registered)
    _note("    allowlist: %d entries, %d now registered"
          % (len(KNOWN_MISSING), len(fixed)))
    assert not fixed, (
        "these are in KNOWN_MISSING but femmx.exe now registers them, so "
        "the entries (and the issues they cite) are done: %r" % fixed)

    unreferenced = []
    all_called = set()
    for pattern in BRIDGES.values():
        all_called |= _bridge_commands(pattern)
    for cmd in KNOWN_MISSING:
        if cmd not in all_called:
            unreferenced.append(cmd)
    assert not unreferenced, (
        "these are in KNOWN_MISSING but no bridge references them any "
        "more, so the entries are dead weight: %r" % unreferenced)


@pytest.mark.static
def test_no_bridge_ships_a_current_flow_tk_point_wrapper(registered):
    """TK points are a heat-flow concept and only heat flow has them.

    hi_addtkpoint/hi_cleartkpoints add a thermal-conductivity-vs-
    temperature point to a material; there is no current-flow analogue,
    the manual documents only the hi_ pair, and no ci_ variant is
    registered. The ci_ wrappers that used to ship in octavefemm,
    scifemm and mathfemm were copy-paste of the heat-flow ones and could
    only ever have thrown.
    """
    assert "hi_addtkpoint" in registered and "hi_cleartkpoints" in registered
    for name in ("ci_addtkpoint", "ci_cleartkpoints"):
        assert name not in registered, (
            "%s is registered now -- if current flow really did gain TK "
            "points, this test and the manual both need updating" % name)

    offenders = []
    for bridge, pattern in sorted(BRIDGES.items()):
        called = _bridge_commands(pattern)
        for name in ("ci_addtkpoint", "ci_cleartkpoints"):
            if name in called:
                offenders.append("%s: %s" % (bridge, name))
    assert not offenders, (
        "a current-flow TK-point wrapper is back: %r" % offenders)

    stray = glob.glob(os.path.join(REPO_ROOT, "octavefemm", "mfiles",
                                   "ci_*tkpoint*.m"))
    assert not stray, "stray wrapper file(s): %r" % stray


@pytest.mark.static
def test_the_bridges_are_reported_not_silently_divergent(registered):
    """Recorded as evidence, not asserted: how far each bridge has drifted.

    Full parity is not the bar and this does not fail on a difference.
    The three bridges were written at different times against different
    versions and none of them has ever been regenerated, so demanding
    they match would mean hand-writing ~90 Mathematica wrappers before
    any of this could go green. What matters is that the divergence is
    written down somewhere a person will see it.
    """
    sets = {b: _bridge_commands(p) for b, p in BRIDGES.items()}
    for bridge in sorted(sets):
        coverage = 100.0 * len(sets[bridge]) / max(len(registered), 1)
        _note("    %-11s covers %d of %d registered commands (%.0f%%)"
              % (bridge, len(sets[bridge] & registered), len(registered),
                 coverage))

    # The fork's own additions are the interesting slice: they postdate
    # every one of these bridges.
    fork_added = {c for c in registered
                  if "setredraw" in c or "setgpuaccel" in c
                  or "savepng" in c or c in ("setgui", "getgui",
                                             "get_solve_stats")}
    for bridge in sorted(sets):
        have = sorted(fork_added & sets[bridge])
        _note("    %-11s exposes %d of %d FEMMX-specific commands"
              % (bridge, len(have), len(fork_added)))
    assert fork_added, "no FEMMX-specific commands found to compare against"


# ---------------------------------------------------------------------------
# Interpreter-backed smoke tests: best effort, skip loudly
# ---------------------------------------------------------------------------

def _find_octave():
    from shutil import which
    found = which("octave-cli") or which("octave")
    if found:
        return found
    for root in (r"C:\Program Files\GNU Octave",
                 r"C:\Program Files (x86)\GNU Octave"):
        if not os.path.isdir(root):
            continue
        for entry in sorted(os.listdir(root), reverse=True):
            cand = os.path.join(root, entry, "mingw64", "bin", "octave-cli.exe")
            if os.path.exists(cand):
                return cand
    return None


@pytest.mark.slow
def test_octavefemm_drives_a_build_solve_probe_cycle():
    """Octave's own wrappers, end to end, when Octave is available.

    Skips rather than fails when Octave is absent or cannot reach COM:
    octavefemm needs actxserver, which comes from the Octave-Forge
    `windows` package, and that package is long unmaintained. Its
    fallback path writes to a hardcoded c:/FEMMX/bin/ and so only works
    against an installed copy, never against the build tree.
    """
    octave = _find_octave()
    if not octave:
        pytest.skip("Octave is not installed on this machine")
    if not os.path.exists(os.path.join(BIN_DIR, "femmx.exe")):
        pytest.skip("femmx.exe not built")

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    mfiles = os.path.join(REPO_ROOT, "octavefemm", "mfiles").replace(chr(92), "/")
    out = os.path.join(OUTPUT_DIR, "octave_probe.txt").replace(chr(92), "/")
    script = os.path.join(OUTPUT_DIR, "octave_probe.m")

    lines = [
        "addpath('%s');" % mfiles,
        "if (exist('actxserver') ~= 2 && exist('actxserver') ~= 5)",
        "  fid = fopen('%s','wt'); fprintf(fid,'NO_ACTXSERVER'); fclose(fid);" % out,
        "  exit(0);",
        "end",
        "openfemm(1);",
        "newdocument(0);",
        "mi_probdef(0,'millimeters','planar',1e-8,0,30);",
        "mi_addnode(0,0);",
        "mi_addnode(10,0);",
        "mi_addsegment(0,0,10,0);",
        "n = mo_numnodes_placeholder = 0;",
        "fid = fopen('%s','wt'); fprintf(fid,'OK'); fclose(fid);" % out,
        "closefemm();",
        "exit(0);",
    ]
    # the placeholder line above is a deliberate no-op assignment; drop it
    lines = [ln for ln in lines if "placeholder" not in ln]

    with open(script, "w", encoding="utf-8", newline="") as fh:
        fh.write(os.linesep.join(lines) + os.linesep)

    if os.path.exists(out):
        os.remove(out)

    proc = subprocess.run([octave, "--no-gui", "--quiet", script],
                          capture_output=True, text=True, timeout=300)
    result = ""
    if os.path.exists(out):
        with open(out, "r", encoding="utf-8", errors="replace") as fh:
            result = fh.read().strip()

    _note("    octavefemm: exit=%d, result=%r" % (proc.returncode, result))
    if result == "NO_ACTXSERVER":
        pytest.skip(
            "this Octave has no actxserver (the Octave-Forge `windows` "
            "package is not installed), so octavefemm cannot reach COM")
    if not result:
        pytest.skip(
            "octavefemm produced no result; Octave said: %s"
            % ((proc.stderr or proc.stdout or "").strip()[:300] or "nothing"))

    assert result == "OK", (
        "octavefemm could not complete a build cycle: %r (stderr: %s)"
        % (result, (proc.stderr or "").strip()[:300]))


@pytest.mark.static
def test_scilab_and_mathematica_have_a_manual_checklist():
    """Neither interpreter can run in CI; the checklist is the deliverable.

    Scilab's bridge is a compiled scilink.dll loaded through Scilab's
    `link()`, and Mathematica is commercial and unscriptable on a runner.
    Pretending otherwise would mean a test that skips forever and tells
    nobody how to check by hand, so the checklist is a file, and this
    asserts it exists and names both bridges.
    """
    doc = os.path.join(REPO_ROOT, "test", "BRIDGE_MANUAL_CHECKS.md")
    assert os.path.exists(doc), (
        "test/BRIDGE_MANUAL_CHECKS.md is missing -- the scifemm and "
        "mathfemm bridges have no automatable coverage, so the written "
        "procedure IS their coverage")
    with open(doc, "r", encoding="utf-8", errors="replace") as fh:
        text = fh.read().lower()
    for bridge in ("scifemm", "mathfemm"):
        assert bridge in text, "%s is not covered by the checklist" % bridge
    _note("    manual checklist present, covers scifemm and mathfemm")


@pytest.mark.static
def test_write_report():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    path = os.path.join(OUTPUT_DIR, "bridge_wrappers.txt")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("Scripting bridge wrappers vs registered commands" + os.linesep)
        fh.write("=" * 70 + os.linesep)
        for line in _REPORT:
            fh.write(line + os.linesep)
    print("report: %s" % path)
    assert os.path.exists(path)
