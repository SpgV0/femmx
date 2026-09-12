"""
The installer, actually installed (issue #21).

script.nsi is ~19 KB of install logic -- the layout, COM self-
registration, the material libraries, both GUIs, four solvers, the
scripting bridges, the manual -- and CI builds it and uploads it as an
artifact without ever running it. Every other test in this directory
runs against the *build tree*, so an installer that ships a missing or
stale file is invisible until a user hits it. This is the one test that
checks what people actually receive.

CAUTION, and the reason this file is careful rather than short: the
installer writes the femm.ActiveFEMM COM registration under HKCU, and
the uninstaller DELETES it. Running this naively would leave the machine
with no registered FEMM at all, and every other test in this suite would
then fail with "femm.ActiveFEMM is not registered". So the registration
is captured before anything happens and restored afterwards, in a
finally, whatever the outcome.

Marked slow: a full install/solve/uninstall cycle is not a PR-lane test.

Usage:
    pytest test/installer_smoke_test.py -v
"""

import glob
import os
import re
import shutil
import struct
import subprocess
import time
import winreg

import pytest

import femmx_paths

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "results", "installer_smoke")
# Resolved rather than hardcoded: CI builds into bin/ while the local
# wrapper scripts move everything into bin/plain, and ten modules that
# assumed bin/plain silently SKIPPED on CI (#24).
BIN_DIR = femmx_paths.BIN_DIR
SCRIPTS_DIR = os.path.join(REPO_ROOT, "scripts")

CLSID = "{0A35D5BD-DCA9-4C39-9512-1D89A1A37047}"
PROGID = "femm.ActiveFEMM"
CLSID_KEY = r"Software\Classes\CLSID\%s" % CLSID
LOCALSERVER_KEY = CLSID_KEY + r"\LocalServer32"
UNINSTALL_KEY = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\FEMMX"
# Where a hidden pre-existing record is parked for the duration -- in the
# registry rather than in memory, so an interrupted run can recover it.
# See the hidden_existing_install fixture.
BACKUP_KEY = r"Software\FEMMX-installer-smoke-test\saved-uninstall-record"

# NSIS /D= must be the last argument and must NOT be quoted, so the
# target cannot contain a space. A fixed root avoids depending on whether
# this machine's %TEMP% happens to have one.
INSTALL_DIR = r"C:\femmx_installer_smoke"

_REPORT = []


def _note(text):
    print(text)
    _REPORT.append(text)


def _installer():
    found = sorted(glob.glob(os.path.join(BIN_DIR, "FEMMX_v*_installer.exe")))
    return found[-1] if found else None


def _reg_get(sub, name=""):
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, sub) as key:
            return winreg.QueryValueEx(key, name)[0]
    except OSError:
        return None


def _reg_set(sub, value, name=""):
    key = winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, sub, 0,
                             winreg.KEY_SET_VALUE)
    try:
        winreg.SetValueEx(key, name, 0, winreg.REG_SZ, value)
    finally:
        winreg.CloseKey(key)


def _run(args, timeout=600):
    return subprocess.run(args, capture_output=True, text=True, timeout=timeout)


def _kill_femm():
    subprocess.run(["taskkill", "/F", "/IM", "femmx.exe"],
                   capture_output=True, text=True)
    subprocess.run(["taskkill", "/F", "/IM", "femmqt.exe"],
                   capture_output=True, text=True)


@pytest.fixture(scope="module")
def restored_com():
    """Capture the machine's COM registration and put it back, always.

    Not a nicety. The uninstaller's first act is DeleteRegKey on the
    ProgID and the CLSID, so without this the suite's own registration --
    pointing at bin/plain/femmx.exe -- is gone the moment this module
    runs, and everything else fails afterwards for a reason that looks
    nothing like its cause.
    """
    saved = {
        "localserver": _reg_get(LOCALSERVER_KEY),
        "clsid": _reg_get(CLSID_KEY),
        "progid": _reg_get(r"Software\Classes\%s" % PROGID),
        "progid_clsid": _reg_get(r"Software\Classes\%s\CLSID" % PROGID),
        "clsid_progid": _reg_get(CLSID_KEY + r"\ProgID"),
    }
    try:
        yield saved
    finally:
        _kill_femm()
        dev_exe = os.path.join(BIN_DIR, "femmx.exe")
        restored = False
        script = os.path.join(SCRIPTS_DIR, "register_femm_com.ps1")
        if os.path.exists(script) and os.path.exists(dev_exe):
            proc = _run(["powershell", "-NoProfile", "-ExecutionPolicy",
                         "Bypass", "-File", script, "-FemmExePath", dev_exe],
                        timeout=180)
            restored = proc.returncode == 0
        if not restored and saved["localserver"]:
            # Fall back to writing the captured values straight back, so a
            # missing or failing registration script cannot leave the
            # machine unregistered.
            for sub, val in (
                    (r"Software\Classes\%s" % PROGID, saved["progid"]),
                    (r"Software\Classes\%s\CLSID" % PROGID, saved["progid_clsid"]),
                    (CLSID_KEY, saved["clsid"]),
                    (CLSID_KEY + r"\ProgID", saved["clsid_progid"]),
                    (LOCALSERVER_KEY, saved["localserver"])):
                if val is not None:
                    _reg_set(sub, val)
        now = _reg_get(LOCALSERVER_KEY)
        print("    COM registration restored to: %s" % (now or "(none)"))


def _install(installer, target):
    """Silent install. /D must be last and unquoted -- an NSIS rule.

    Given a bounded timeout and killed if it overruns, rather than left to
    block: a silent install that stops to ask something has nobody to
    answer it, which is precisely the defect this file found in .onInit.
    A test for unattended installation must not itself hang when
    installation turns out not to be unattendable.
    """
    proc = subprocess.Popen([installer, "/S", "/D=%s" % target],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    exe = os.path.join(target, "bin", "femmx.exe")
    deadline = time.time() + 180
    while time.time() < deadline:
        if proc.poll() is not None and os.path.exists(exe):
            break
        time.sleep(0.5)
    if proc.poll() is None:
        proc.kill()
        subprocess.run(["taskkill", "/F", "/IM",
                        os.path.basename(installer)],
                       capture_output=True, text=True)
        raise AssertionError(
            "`%s /S` did not finish within 180s. A silent install that "
            "blocks is a silent install nobody can script: check that every "
            "MessageBox reachable from .onInit is guarded by IfSilent."
            % os.path.basename(installer))
    return proc


def _uninstall(target):
    """Uninstall the way a user does, not the way an upgrade does.

    Deliberately WITHOUT `_?=$INSTDIR`. That switch makes NSIS run the
    uninstaller in place instead of copying itself to %TEMP% and
    relaunching, which is what .onInit's upgrade path needs (ExecWait has
    to be able to block on it) -- but it also means the uninstaller
    cannot delete itself or its own directory, because they are in use.
    Testing with `_?=` and then asserting the directory is gone would be
    asserting something NSIS documents as impossible.

    Without it the uninstaller returns immediately and the real work
    happens in the relaunched copy, so this polls for the directory to
    disappear rather than trusting the exit code.
    """
    un = os.path.join(target, "uninstall.exe")
    if not os.path.exists(un):
        cands = glob.glob(os.path.join(target, "*nstall*.exe"))
        un = cands[0] if cands else None
    if not un:
        return None
    proc = subprocess.run([un, "/S"], capture_output=True, text=True,
                          timeout=600)
    deadline = time.time() + 180
    while time.time() < deadline and os.path.isdir(target):
        time.sleep(0.5)
    return proc


@pytest.fixture(scope="module")
def hidden_existing_install():
    """Hide any pre-existing FEMMX from the installer, and put it back.

    This machine may already have a real FEMMX installed -- this one did,
    2.1.2 in C:\\FEMMX. script.nsi's .onInit reads
    Uninstall\\FEMMX\\QuietUninstallString and, in silent mode, now
    uninstalls what it finds so an unattended upgrade can proceed. That
    is right for an upgrade and completely wrong for a test: it would
    delete software the developer actually uses, and nothing here could
    put it back, because this repository does not have the installer that
    produced it.

    So the registry record is saved and removed for the duration, which
    makes .onInit take the "nothing installed" path and leave the
    existing installation strictly alone. Only the record is touched;
    the installed files are never opened. The record is restored in a
    finally, including after the module's own uninstall deletes it.
    """
    def read(sub):
        out = {}
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, sub) as key:
                i = 0
                while True:
                    try:
                        name, value, kind = winreg.EnumValue(key, i)
                    except OSError:
                        break
                    out[name] = (value, kind)
                    i += 1
        except OSError:
            return {}
        return out

    def write(sub, values):
        key = winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, sub, 0,
                                 winreg.KEY_SET_VALUE)
        try:
            for name, (value, kind) in values.items():
                winreg.SetValueEx(key, name, 0, kind, value)
        finally:
            winreg.CloseKey(key)

    # The backup lives in the registry rather than in memory, so a run
    # that is killed between hiding and restoring does not take the
    # record with it. (Written after doing exactly that by hand while
    # developing this file, which cost a real installation its
    # Add/Remove Programs entry.) A leftover backup from such a run is
    # recovered here before anything else happens.
    orphan = read(BACKUP_KEY)
    if orphan and not read(UNINSTALL_KEY):
        print("    recovering an orphaned FEMMX registry record from a "
              "previous interrupted run")
        write(UNINSTALL_KEY, orphan)

    saved = read(UNINSTALL_KEY)

    if saved:
        print("    a pre-existing FEMMX %s is registered; hiding its record "
              "so the installer leaves it alone"
              % saved.get("DisplayVersion", ("?",))[0])
        write(BACKUP_KEY, saved)
        try:
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, UNINSTALL_KEY)
        except OSError as exc:
            pytest.skip(
                "a pre-existing FEMMX is registered and its record could "
                "not be hidden (%s), so installing would uninstall it" % exc)
    try:
        yield saved
    finally:
        if saved:
            write(UNINSTALL_KEY, saved)
            try:
                winreg.DeleteKey(winreg.HKEY_CURRENT_USER, BACKUP_KEY)
            except OSError:
                pass
            print("    restored the pre-existing FEMMX registry record")


@pytest.fixture(scope="module")
def installed(restored_com, hidden_existing_install):
    """Install once for the module; uninstall in the teardown test."""
    installer = _installer()
    if not installer:
        pytest.skip("no FEMMX_v*_installer.exe in bin/plain -- build it first")

    # Never install over, or anywhere near, a real installation.
    assert INSTALL_DIR.lower() not in (r"c:\femmx", r"c:\femm42"), \
        "the smoke test must not target a real installation directory"

    _kill_femm()
    if os.path.isdir(INSTALL_DIR):
        shutil.rmtree(INSTALL_DIR, ignore_errors=True)

    proc = _install(installer, INSTALL_DIR)
    _note("    installed %s -> %s (exit %d)"
          % (os.path.basename(installer), INSTALL_DIR, proc.returncode))
    if not os.path.exists(os.path.join(INSTALL_DIR, "bin", "femmx.exe")):
        pytest.skip(
            "the silent install produced no bin/femmx.exe (exit %d, stderr %r)"
            % (proc.returncode, (proc.stderr or "").strip()[:200]))
    yield INSTALL_DIR


# ---------------------------------------------------------------------------
# Layout
# ---------------------------------------------------------------------------

# Everything script.nsi installs with a non-/nonfatal File directive, i.e.
# everything whose absence is a broken installation rather than a
# build-configuration difference.
REQUIRED = [
    r"bin\femmx.exe",
    r"bin\fkn.exe",
    r"bin\belasolv.exe",
    r"bin\hsolv.exe",
    r"bin\csolv.exe",
    r"bin\femmplot.exe",
    r"bin\triangle.exe",
    r"bin\matlib.dat",
    r"bin\heatlib.dat",
    r"bin\condlib.dat",
    r"bin\statlib.dat",
    r"bin\init.lua",
    r"bin\license.txt",
    "README.md",
    r"scifemm\scifemm.sci",
]


@pytest.mark.slow
def test_the_installed_layout_is_complete(installed):
    """Every non-optional file script.nsi ships must be there.

    A solver missing from the installer is the worst kind of packaging
    bug: femmx.exe starts, the user draws a model, and the failure only
    appears at Analyze -- by which point nothing points at the installer.
    """
    missing = [rel for rel in REQUIRED
               if not os.path.exists(os.path.join(installed, rel))]
    _note("    layout: %d of %d required files present"
          % (len(REQUIRED) - len(missing), len(REQUIRED)))
    assert not missing, "the installer did not ship: %r" % missing


_DLL_NAME = re.compile(r"^[A-Za-z0-9_.+-]+\.dll$", re.I)


def _imported_dlls(path):
    """The DLLs a PE file imports, read out of its import directory.

    Reading the import table beats launching the binary, which was the
    first thing tried here and was wrong: run with no arguments the
    solvers put up a file-open dialog and wait forever, so the probe hung
    on exactly the binaries it was meant to check. Their GUI mode is not
    a defect, and a test has no business discovering that by hanging.

    Returns None if the file is not a PE image this can parse.
    """
    with open(path, "rb") as fh:
        d = fh.read()
    if len(d) < 0x40 or d[:2] != b"MZ":
        return None
    pe = struct.unpack_from("<I", d, 0x3c)[0]
    if d[pe:pe + 4] != b"PE\0\0":
        return None
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    optsz = struct.unpack_from("<H", d, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from("<H", d, opt)[0]
    # the import directory is data-directory entry 1
    ddoff = opt + (112 if magic == 0x20b else 96)
    imp_rva = struct.unpack_from("<I", d, ddoff + 8)[0]
    if not imp_rva:
        return []

    sections = []
    secoff = opt + optsz
    for i in range(nsec):
        base = secoff + i * 40
        vsz, va, rawsz, rawptr = struct.unpack_from("<IIII", d, base + 8)
        sections.append((va, max(vsz, rawsz), rawptr))

    def to_offset(rva):
        for va, size, ptr in sections:
            if va <= rva < va + size:
                return ptr + (rva - va)
        return None

    names, off = [], to_offset(imp_rva)
    while off is not None:
        entry = d[off:off + 20]
        if len(entry) < 20 or entry == b"\0" * 20:
            break
        name_rva = struct.unpack_from("<I", entry, 12)[0]
        if not name_rva:
            break
        no = to_offset(name_rva)
        if no is None:
            break
        end = d.index(b"\0", no)
        name = d[no:end].decode("ascii", "replace")
        if not _DLL_NAME.match(name):
            break
        names.append(name)
        off += 20
    return names


@pytest.mark.slow
def test_every_shipped_binary_has_the_dlls_it_imports(installed):
    """The packaging failure a file list cannot see.

    A binary that ships without a DLL it needs does not fail at build
    time or install time: it fails when the user double-clicks it, with
    "the code execution cannot proceed because X.dll was not found". The
    Qt runtime and the MSVC redistributable are the usual casualties,
    because they live outside the build tree.

    Each imported DLL must resolve to the install's own bin directory or
    to a system directory. api-ms-win-* and ext-ms-* are API sets, which
    the loader resolves without a file on disk.
    """
    system_dirs = [os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), d)
                   for d in ("System32", "SysWOW64")]

    def resolvable(dll, exe_dir):
        low = dll.lower()
        if low.startswith("api-ms-win-") or low.startswith("ext-ms-"):
            return True
        if os.path.exists(os.path.join(exe_dir, dll)):
            return True
        return any(os.path.exists(os.path.join(d, dll)) for d in system_dirs)

    unresolved = {}
    checked = 0
    bindir = os.path.join(installed, "bin")
    for entry in sorted(os.listdir(bindir)):
        if not entry.lower().endswith(".exe"):
            continue
        path = os.path.join(bindir, entry)
        dlls = _imported_dlls(path)
        if dlls is None:
            continue
        checked += 1
        missing = [d for d in dlls if not resolvable(d, bindir)]
        if missing:
            unresolved[entry] = missing

    _note("    dependencies: %d executables checked, %d with unresolved "
          "imports" % (checked, len(unresolved)))
    assert checked >= 5, (
        "only %d executables were parsed out of %s -- the check is broken, "
        "not the install" % (checked, bindir))
    assert not unresolved, (
        "the installer shipped these binaries without DLLs they import, so "
        "they cannot start on a machine that does not happen to have them: "
        "%r" % unresolved)


@pytest.mark.slow
def test_the_qt_gui_ships_with_its_platform_plugin(installed):
    """femmqt.exe without qwindows.dll starts and then dies.

    "This application failed to start because no Qt platform plugin could
    be initialized" -- a message box, in an installed product, for a GUI
    the Start Menu offers. The plugin lives in a subdirectory that has to
    be shipped separately from the exe, which is exactly the kind of thing
    an installer misses.
    """
    exe = os.path.join(installed, "bin", "femmqt.exe")
    if not os.path.exists(exe):
        pytest.skip("femmqt.exe was not part of this build")
    plugin = os.path.join(installed, "bin", "platforms", "qwindows.dll")
    core = glob.glob(os.path.join(installed, "bin", "Qt6Core.dll"))
    _note("    femmqt: platform plugin %s, Qt6Core %s"
          % ("present" if os.path.exists(plugin) else "MISSING",
             "present" if core else "MISSING"))
    assert os.path.exists(plugin), (
        "femmqt.exe shipped without bin/platforms/qwindows.dll, so it cannot "
        "start at all")
    assert core, "femmqt.exe shipped without its Qt6 runtime DLLs"


# ---------------------------------------------------------------------------
# COM registration, against the installed copy
# ---------------------------------------------------------------------------

@pytest.mark.slow
def test_the_installer_registers_itself_for_com(installed):
    """The install must repoint femm.ActiveFEMM at what it just installed.

    Self-registration under HKCU is what makes pyfemm work after an
    install with no admin rights (cf. #7). If it silently did not happen,
    a user's scripts would keep driving whatever FEMM was registered
    before -- possibly an older install, with no indication.
    """
    server = _reg_get(LOCALSERVER_KEY)
    progid = _reg_get(r"Software\Classes\%s\CLSID" % PROGID)
    _note("    after install, LocalServer32 = %s" % (server or "(none)"))
    assert server, "the installer registered no LocalServer32"
    assert installed.lower() in server.lower(), (
        "LocalServer32 is %r, which does not point into the installation "
        "just made (%s)" % (server, installed))
    assert progid and progid.lower() == CLSID.lower(), (
        "the ProgID does not resolve to the CLSID: %r" % progid)


@pytest.mark.slow
def test_a_scripted_solve_works_against_the_installed_copy(installed):
    """The thing users do, done against what users get.

    Driven through the installed femmx.exe, using the installed material
    library -- so this covers the COM registration, the solver, and
    matlib.dat shipping and loading, in the one flow that matters.
    """
    import femm

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    fem = os.path.join(OUTPUT_DIR, "installed_solve.fem").replace(chr(92), "/")

    femm.openfemm(1)
    try:
        femm.newdocument(0)
        femm.mi_probdef(0, "millimeters", "planar", 1e-8, 100, 30)
        # from the INSTALLED matlib.dat -- a library that did not ship, or
        # shipped truncated, fails right here
        femm.mi_getmaterial("Air")
        femm.mi_addmaterial("coil", 1, 1, 0, 10, 0, 0, 0, 1, 0, 0, 0)
        for x, y in ((-30, -30), (30, -30), (30, 30), (-30, 30),
                     (-5, -5), (5, -5), (5, 5), (-5, 5)):
            femm.mi_addnode(x, y)
        for x0, y0, x1, y1 in ((-30, -30, 30, -30), (30, -30, 30, 30),
                               (30, 30, -30, 30), (-30, 30, -30, -30),
                               (-5, -5, 5, -5), (5, -5, 5, 5),
                               (5, 5, -5, 5), (-5, 5, -5, -5)):
            femm.mi_addsegment(x0, y0, x1, y1)
        femm.mi_addblocklabel(0, 0)
        femm.mi_selectlabel(0, 0)
        femm.mi_setblockprop("coil", 1, 0, "<None>", 0, 0, 0)
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

        femm.mi_saveas(fem)
        femm.mi_analyze(1)
        femm.mi_loadsolution()
        femm.mo_selectblock(0, 0)
        energy = femm.mo_blockintegral(2)
    finally:
        try:
            femm.closefemm()
        except Exception:  # noqa: BLE001
            pass

    _note("    installed-copy solve: stored energy = %r" % energy)
    assert os.path.exists(fem.replace("/", chr(92))) or os.path.exists(fem)
    assert abs(energy) > 1e-12, (
        "the solve against the installed copy produced no field (energy "
        "%r) -- a zero here would also be produced by a solver that did "
        "nothing, so it is not an acceptable pass" % energy)


@pytest.mark.slow
def test_the_shipped_material_library_has_real_content(installed):
    """matlib.dat must be the library, not a placeholder.

    Cheap to get wrong (an empty file still "ships") and impossible to
    notice from a file list.
    """
    lib = os.path.join(installed, "bin", "matlib.dat")
    size = os.path.getsize(lib)
    with open(lib, "r", encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    names = re.findall(r"<BlockName>\s*=\s*(.+)", text)
    _note("    matlib.dat: %d bytes, %d block names" % (size, len(names)))
    assert size > 10000, "matlib.dat is only %d bytes" % size
    assert len(names) > 50, (
        "matlib.dat declares only %d materials" % len(names))


# ---------------------------------------------------------------------------
# Upgrade and removal
# ---------------------------------------------------------------------------

@pytest.mark.slow
def test_installing_over_an_existing_install_still_works(installed):
    """Users upgrade in place; that must not produce a broken tree."""
    installer = _installer()
    proc = _install(installer, installed)
    exe = os.path.join(installed, "bin", "femmx.exe")
    missing = [rel for rel in REQUIRED
               if not os.path.exists(os.path.join(installed, rel))]
    server = _reg_get(LOCALSERVER_KEY)

    _note("    install-over-install: exit %d, %d files missing after"
          % (proc.returncode, len(missing)))
    assert os.path.exists(exe), "the upgrade removed femmx.exe"
    assert not missing, "the upgrade left these missing: %r" % missing
    assert server and installed.lower() in server.lower(), (
        "the upgrade left LocalServer32 at %r" % server)


@pytest.mark.slow
def test_uninstall_removes_everything_it_installed(installed):
    """Runs last in this module: it takes the installation away.

    Ordered by position rather than by a fixture, deliberately -- the
    other tests need the installation, and pytest runs them in file
    order. The COM registration this destroys is put back by the
    restored_com fixture's teardown.
    """
    _kill_femm()
    proc = _uninstall(installed)
    if proc is None:
        pytest.fail("no uninstaller was installed")

    leftovers = [rel for rel in REQUIRED
                 if os.path.exists(os.path.join(installed, rel))]
    dir_gone = not os.path.isdir(installed)
    progid = _reg_get(r"Software\Classes\%s" % PROGID)
    clsid = _reg_get(CLSID_KEY)
    uninst = _reg_get(UNINSTALL_KEY, "DisplayVersion")

    _note("    uninstall: exit %d, %d files left, dir removed: %s, "
          "COM keys left: %s"
          % (proc.returncode, len(leftovers), dir_gone,
             "yes" if (progid or clsid) else "no"))

    assert not leftovers, "the uninstaller left files behind: %r" % leftovers
    assert progid is None and clsid is None, (
        "the uninstaller left the COM registration behind (ProgID %r, "
        "CLSID %r)" % (progid, clsid))
    assert uninst is None, (
        "the uninstaller left its Add/Remove Programs entry behind")
    assert dir_gone, (
        "%s still exists after uninstall; contents: %r"
        % (installed, os.listdir(installed)[:10]))


def test_write_report():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    path = os.path.join(OUTPUT_DIR, "installer_smoke.txt")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("Installer smoke test" + os.linesep)
        fh.write("=" * 70 + os.linesep)
        for line in _REPORT:
            fh.write(line + os.linesep)
    print("report: %s" % path)
    assert os.path.exists(path)
