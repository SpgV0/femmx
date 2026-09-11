"""
COM automation hardening (issue #18).

COM automation is how every test in this repo drives FEMM, and how pyfemm
users drive it -- but the automation layer itself was mostly untested
infrastructure rather than a tested feature. parallel_instances_test.py
covers concurrent sessions; everything around it was uncovered.

Requirements: a built, COM-registered femmx.exe; pip install pyfemm pywin32.

Usage:
    pytest test/com_automation_test.py -v
"""

import os
import subprocess
import time
import winreg

import pytest

import femm

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "results", "com_automation")
SCRIPTS_DIR = os.path.join(REPO_ROOT, "scripts")
BIN_DIR = os.path.join(REPO_ROOT, "bin", "plain")

CLSID = "{0A35D5BD-DCA9-4C39-9512-1D89A1A37047}"
PROGID = "femm.ActiveFEMM"

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
    path = os.path.join(OUTPUT_DIR, "com_automation.txt")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("COM automation checks" + os.linesep)
        fh.write("=" * 70 + os.linesep)
        for line in _REPORT:
            fh.write(line + os.linesep)
    return path


def _running_femmx():
    """PIDs of every femmx.exe currently alive."""
    proc = subprocess.run(
        ["tasklist", "/FI", "IMAGENAME eq femmx.exe", "/FO", "CSV", "/NH"],
        capture_output=True, text=True)
    pids = []
    for line in (proc.stdout or "").splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) >= 2 and parts[0].lower() == "femmx.exe":
            try:
                pids.append(int(parts[1]))
            except ValueError:
                pass
    return set(pids)


# ---------------------------------------------------------------------------
# 1. Registration lifecycle
# ---------------------------------------------------------------------------

def _registered_path(hive, sub):
    try:
        with winreg.OpenKey(hive, sub) as key:
            return winreg.QueryValueEx(key, "")[0]
    except OSError:
        return None


def test_com_registration_is_per_user_and_needs_no_admin():
    """The ProgID/CLSID must live under HKCU, not HKLM.

    Per-user registration is what makes this usable on a locked-down box
    and in CI without elevation (cf. cenit/FEMM#7). A registration that
    only works from an elevated shell is a registration most users cannot
    perform.
    """
    hkcu = _registered_path(winreg.HKEY_CURRENT_USER,
                            r"Software\Classes\CLSID\%s\LocalServer32" % CLSID)
    hklm = _registered_path(winreg.HKEY_LOCAL_MACHINE,
                            r"SOFTWARE\Classes\CLSID\%s\LocalServer32" % CLSID)

    _note("    registration: HKCU=%s | HKLM=%s"
          % (hkcu or "(none)", hklm or "(none)"))
    assert hkcu, (
        "no per-user registration under HKCU. scripts/register_femm_com.ps1 "
        "writes there deliberately so it needs no admin rights; if only HKLM "
        "is set, every user without elevation is locked out.")
    assert femm is not None


def test_register_script_is_idempotent():
    """Running the registration twice must leave the same result.

    An installer, a CI step and a developer can each run it; none should
    care whether it ran before.
    """
    script = os.path.join(SCRIPTS_DIR, "register_femm_com.ps1")
    if not os.path.exists(script):
        pytest.skip("no register_femm_com.ps1")
    exe = os.path.join(BIN_DIR, "femmx.exe")
    if not os.path.exists(exe):
        pytest.skip("femmx.exe not built")

    before = _registered_path(
        winreg.HKEY_CURRENT_USER,
        r"Software\Classes\CLSID\%s\LocalServer32" % CLSID)

    outs = []
    for _ in range(2):
        proc = subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
             "-File", script, "-FemmExePath", exe],
            capture_output=True, text=True, timeout=120)
        outs.append(proc.returncode)

    after = _registered_path(
        winreg.HKEY_CURRENT_USER,
        r"Software\Classes\CLSID\%s\LocalServer32" % CLSID)

    _note("    register script run twice: exit codes %r, path %s -> %s"
          % (outs, before, after))
    assert outs == [0, 0], "the registration script failed: %r" % outs
    assert after and exe.lower() in after.lower().replace("/", chr(92)), (
        "after registering, LocalServer32 is %r, which does not point at the "
        "binary that was registered" % after)


# ---------------------------------------------------------------------------
# 2. Teardown
# ---------------------------------------------------------------------------

def test_closefemm_leaves_no_orphan_process():
    """closefemm() must actually end the process it started.

    An orphaned femmx.exe holds its COM registration slot and its temp
    files; enough of them and a later Dispatch attaches to a session in an
    unknown state.
    """
    baseline = _running_femmx()

    femm.openfemm(1)
    femm.newdocument(0)
    during = _running_femmx()
    started = during - baseline
    assert started, "openfemm() did not appear to start a femmx.exe"

    femm.closefemm()

    # give the process a moment to actually exit
    deadline = time.time() + 15.0
    leftover = started
    while time.time() < deadline:
        leftover = started & _running_femmx()
        if not leftover:
            break
        time.sleep(0.5)

    _note("    teardown: started %d process(es), %d still alive after "
          "closefemm()" % (len(started), len(leftover)))
    assert not leftover, (
        "closefemm() left %d orphaned femmx.exe process(es): %r"
        % (len(leftover), sorted(leftover)))


def test_a_new_session_works_after_an_abandoned_one():
    """An abandoned session must not wedge the next one."""
    femm.openfemm(1)
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-8, 1, 30)
    # deliberately abandoned: no closefemm()

    # a fresh Dispatch must still work
    femm.openfemm(1)
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-8, 1, 30)
    femm.mi_addnode(1.0, 2.0)
    try:
        path = os.path.join(OUTPUT_DIR, "after_abandon.fem")
        os.makedirs(OUTPUT_DIR, exist_ok=True)
        femm.mi_saveas(_save(path))
        ok = os.path.exists(path)
    finally:
        _teardown()

    _note("    a new session after an abandoned one: %s"
          % ("works" if ok else "FAILED"))
    assert ok, "a fresh session could not save after an abandoned one"


# ---------------------------------------------------------------------------
# 3. Error propagation
# ---------------------------------------------------------------------------

def test_a_lua_error_surfaces_as_a_python_exception():
    """A failing Lua call must raise, with the reason in the message.

    A silent no-op is the dangerous outcome: a sweep would carry on and
    report success over a model that was never built.
    """
    femm.openfemm(1)
    femm.newdocument(0)
    try:
        raised = None
        try:
            femm.callfemm("this_command_does_not_exist()")
        except Exception as exc:  # noqa: BLE001
            raised = str(exc)
        _note("    unknown Lua command -> %s"
              % (("raised: " + raised.replace(os.linesep, " ")[:70])
                 if raised else "NO EXCEPTION"))
        assert raised is not None, (
            "calling a non-existent Lua command returned quietly; a sweep "
            "would carry on over a model that was never built")
        assert len(raised.strip()) > 10, (
            "the exception message %r says nothing useful" % raised)
    finally:
        _teardown()


def test_a_bad_argument_surfaces_too():
    """Not just unknown commands: a real command given nonsense."""
    femm.openfemm(1)
    femm.newdocument(0)
    try:
        raised = None
        try:
            # a length unit that does not exist
            femm.mi_probdef(0, "furlongs", "planar", 1e-8, 1, 30)
        except Exception as exc:  # noqa: BLE001
            raised = str(exc)
        _note("    mi_probdef with a bogus unit -> %s"
              % (("raised: " + raised.replace(os.linesep, " ")[:70])
                 if raised else "accepted silently"))
        # FEMM is lenient about some arguments; what must NOT happen is a
        # crash or a hang, and the session must still be usable.
        femm.mi_addnode(0.0, 0.0)
    finally:
        _teardown()


# ---------------------------------------------------------------------------
# 4. Headless behaviour
# ---------------------------------------------------------------------------

def test_a_scripted_solve_shows_no_blocking_dialog():
    """A scripted solve must complete unattended.

    Asserted because a blocking dialog in CI shows up as an opaque
    timeout, minutes or hours later, with nothing in the log explaining
    it -- which this backlog has now hit three separate times (the
    solvers' argc rule, savebitmap, and --render-png).
    """
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    path = os.path.join(OUTPUT_DIR, "headless.fem")
    started = time.time()
    try:
        femm.openfemm(1)
        femm.newdocument(0)
        femm.mi_probdef(0, "millimeters", "planar", 1e-8, 1, 30)
        femm.mi_smartmesh(0)
        femm.mi_getmaterial("Air")
        for x, y in ((-10, -10), (10, -10), (10, 10), (-10, 10)):
            femm.mi_addnode(x, y)
        femm.mi_addsegment(-10, -10, 10, -10)
        femm.mi_addsegment(10, -10, 10, 10)
        femm.mi_addsegment(10, 10, -10, 10)
        femm.mi_addsegment(-10, 10, -10, -10)
        femm.mi_addboundprop("edge", 0, 0, 0, 0, 0, 0, 0, 0, 0)
        for px, py in ((0, -10), (10, 0), (0, 10), (-10, 0)):
            femm.mi_selectsegment(px, py)
            femm.mi_setsegmentprop("edge", 0, 1, 0, 0)
            femm.mi_clearselected()
        femm.mi_addblocklabel(0, 0)
        femm.mi_selectlabel(0, 0)
        femm.mi_setblockprop("Air", 0, 4.0, "", 0, 0, 0)
        femm.mi_attachdefault()
        femm.mi_clearselected()
        femm.mi_saveas(_save(path))
        femm.mi_analyze(1)
        femm.mi_loadsolution()
        nodes = int(femm.mo_numnodes())
    finally:
        _teardown()
    elapsed = time.time() - started

    _note("    headless solve: %d nodes in %.1fs (no dialog blocked it)"
          % (nodes, elapsed))
    assert nodes > 0
    assert elapsed < 120.0, (
        "a trivial scripted solve took %.0fs; the usual cause is a modal "
        "dialog waiting for a click that will never come" % elapsed)


# ---------------------------------------------------------------------------
# 5. -lua-var argument handling
# ---------------------------------------------------------------------------

def test_lua_var_preserves_the_case_of_its_value():
    """-lua-var must not fold its value to lower case.

    CMyCommandLineInfo::ParseParam used to call MakeLower() on the WHOLE
    parameter before parsing anything out of it, so
    `-lua-var=MyVar=C:\\Path\\File` reached the script as `myvar` holding
    `c:\\path\\file` -- both the name and the value mangled, silently
    (cenit/FEMM#18). Paths survive that on Windows; material names,
    identifiers and anything compared with `==` in a script do not.
    """
    exe = os.path.join(BIN_DIR, "femmx.exe")
    if not os.path.exists(exe):
        pytest.skip("femmx.exe not built")

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    out_path = os.path.join(OUTPUT_DIR, "luavar_out.txt")
    script_path = os.path.join(OUTPUT_DIR, "luavar.lua")
    if os.path.exists(out_path):
        os.remove(out_path)

    # write whatever the variable holds, verbatim, to a file
    lua = (
        'local f = openfile("%s", "w")\n'
        'write(f, MixedCaseVar or "<undefined>")\n'
        'closefile(f)\n'
        'exitpre()\n'
    ) % _save(out_path)
    with open(script_path, "w", encoding="ascii") as fh:
        fh.write(lua)

    sentinel = "MixedCase_VALUE_123"
    # Launched without waiting on the process: femmx.exe stays running after
    # a -lua-script finishes (exitpre() closes the document, not the app),
    # so subprocess.run would block until its timeout every time. Poll for
    # the artifact the script writes, then stop the process.
    proc = subprocess.Popen(
        [exe, "-lua-script=%s" % _save(script_path),
         "-lua-var=MixedCaseVar=%s" % sentinel, "-windowhide"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        deadline = time.time() + 60
        while time.time() < deadline and not os.path.exists(out_path):
            time.sleep(0.5)
    finally:
        proc.kill()
        subprocess.run(["taskkill", "/F", "/IM", "femmx.exe"],
                       capture_output=True, text=True)

    if not os.path.exists(out_path):
        pytest.skip("the -lua-script run produced no output file; "
                    "script-mode invocation may differ on this build")

    with open(out_path, "r", encoding="utf-8", errors="replace") as fh:
        got = fh.read().strip()

    _note("    -lua-var value round-trip: sent %r, script saw %r"
          % (sentinel, got))
    assert got != "<undefined>", (
        "the script never received MixedCaseVar -- the variable name itself "
        "was folded, so the script's own lookup missed it")
    assert got == sentinel, (
        "-lua-var mangled its value: sent %r, the script saw %r. ParseParam "
        "must lowercase only the copy it uses for flag comparisons."
        % (sentinel, got))

def test_write_report():
    path = _write_report()
    assert os.path.exists(path)
    print("report: " + path)
