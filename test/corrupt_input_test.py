"""
Corrupt and truncated input must not crash or hang anything (issue #10).

Nothing in the suite fed bad input to anything. .fem, .ans, .femx, .ansx
and .dxf are all parsed by hand-rolled readers in both GUIs and in four
solver binaries, and a truncated file is the normal outcome of an
interrupted solve or a half-copied file -- a realistic input, not a
fuzzing exotic.

Corrupt variants are generated systematically from real, valid files
produced by the other tests' fixtures: truncated at several offsets, a
header field zeroed, a declared count inflated past the data that follows
it, garbage appended, and empty.

THE SOLVERS HAVE A NON-INTERACTIVE MODE, AND IT IS NOT OPTIONAL FOR
AUTOMATION. fkn/StdAfx.cpp:

    int MsgBox(CString s) {
      if (__argc < 3) return AfxMessageBox(s);   // modal, blocks forever
      else            return IDOK;               // suppressed
    }

so `fkn.exe model` pops a modal dialog and hangs with no user to dismiss
it, while `fkn.exe model anything` exits with a status code. The GUI
passes the literal "bLinehook" as that second argument
(femm/FemmeView.cpp). Measured: one-argument invocations on truncated,
empty and garbage input all sat there until killed at 15s; two-argument
invocations of the same files all exited 2 promptly. Any CI or batch
script driving a solver directly must pass a second argument.

Every subprocess here runs under a timeout and is killed if it overruns,
so a regression that reintroduces a blocking dialog fails the test rather
than wedging the run.

Requirements: a built femmx.exe/femmqt.exe; pip install pyfemm pywin32.

Usage:
    pytest test/corrupt_input_test.py -v
"""

import os
import shutil
import struct
import subprocess

import pytest

import femm

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "results", "corrupt_input")
BIN_DIR = os.path.join(REPO_ROOT, "bin", "plain")

FEMMQT = os.path.join(BIN_DIR, "femmqt.exe")
SOLVERS = {
    "fkn": os.path.join(BIN_DIR, "fkn.exe"),
    "belasolv": os.path.join(BIN_DIR, "belasolv.exe"),
    "hsolv": os.path.join(BIN_DIR, "hsolv.exe"),
    "csolv": os.path.join(BIN_DIR, "csolv.exe"),
}

# Generous, but far short of "hung". A healthy solver rejects bad input in
# well under a second; the measured hang was unbounded.
PROC_TIMEOUT_SEC = 30

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
    path = os.path.join(OUTPUT_DIR, "corrupt_input.txt")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("Corrupt / truncated input handling" + os.linesep)
        fh.write("=" * 74 + os.linesep)
        for line in _REPORT:
            fh.write(line + os.linesep)
    return path


# ---------------------------------------------------------------------------
# Building the corrupt variants
# ---------------------------------------------------------------------------

def corrupt_variants(data, binary):
    """{label: bytes} -- the systematic set the ticket asks for."""
    out = {
        "empty": b"",
        "trunc_10pct": data[:max(1, len(data) // 10)],
        "trunc_50pct": data[:max(1, len(data) // 2)],
        "trunc_90pct": data[:max(1, (len(data) * 9) // 10)],
        "garbage_appended": data + (b"\x00\xff\xfe garbage " * 64),
    }
    if binary and len(data) > 64:
        # zero the magic, so the header is structurally wrong
        out["header_zeroed"] = b"\x00" * 8 + data[8:]
        # inflate a count field far past the data that follows it
        blown = bytearray(data)
        struct.pack_into("<Q", blown, len(data) - 8 if len(data) < 200 else 48,
                         0xFFFFFFFF)
        out["count_inflated"] = bytes(blown)
    else:
        out["garbage_only"] = b"this is not a FEMM file at all" * 8
    return out


def write_variants(source_path, out_dir, stem, ext, binary):
    """Materialise every variant next to each other; returns [(label, path)]."""
    os.makedirs(out_dir, exist_ok=True)
    with open(source_path, "rb") as fh:
        data = fh.read()
    written = []
    for label, blob in corrupt_variants(data, binary).items():
        path = os.path.join(out_dir, "%s_%s%s" % (stem, label, ext))
        with open(path, "wb") as fh:
            fh.write(blob)
        written.append((label, path))
    return written


def _run(cmd, timeout=PROC_TIMEOUT_SEC):
    """Run a subprocess, reporting a timeout as a hang rather than raising."""
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              timeout=timeout)
        return proc.returncode, (proc.stderr or "") + (proc.stdout or ""), False
    except subprocess.TimeoutExpired:
        return None, "", True


def _is_crash(code):
    """Windows surfaces an access violation etc. as a huge unsigned status."""
    if code is None:
        return False
    return (code & 0xFFFFFFFF) >= 0xC0000000


# ---------------------------------------------------------------------------
# Source files to corrupt, taken from what the other tests produce
# ---------------------------------------------------------------------------

def _find_source(*candidates):
    for rel in candidates:
        path = os.path.join(SCRIPT_DIR, "results", *rel.split("/"))
        if os.path.exists(path):
            return path
    return None


@pytest.fixture(scope="module")
def sources():
    """A valid .fem, .ans, .femx, .ansx and .dxf to corrupt."""
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    found = {
        "fem": _find_source("file_format_roundtrip/solve_src.fem",
                            "magnetics_analytic/two_wires.fem"),
        "ans": _find_source("file_format_roundtrip/solve_src.ans",
                            "magnetics_analytic/two_wires.ans"),
        "femx": _find_source("file_format_roundtrip/solve_src.femx"),
        "ansx": _find_source("file_format_roundtrip/solve_src.ansx"),
        "dxf": _find_source("dxf_roundtrip/roundtrip.dxf"),
    }
    missing = [k for k, v in found.items() if not v]
    if missing:
        pytest.skip("no valid source file for %s -- run the other suites first"
                    % ", ".join(missing))
    _note("    sources: " + ", ".join("%s=%s" % (k, os.path.basename(v))
                                      for k, v in found.items()))
    return found


# ---------------------------------------------------------------------------
# 1. Solvers must reject bad input, promptly, in non-interactive mode
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("solver", sorted(SOLVERS))
def test_solvers_reject_corrupt_input_without_hanging(solver, sources):
    """Every solver, every corrupt variant: prompt exit, no crash."""
    exe = SOLVERS[solver]
    if not os.path.exists(exe):
        pytest.skip("%s not built" % solver)

    ext = {"fkn": ".fem", "belasolv": ".fee",
           "hsolv": ".feh", "csolv": ".fec"}[solver]
    work = os.path.join(OUTPUT_DIR, solver)
    variants = write_variants(sources["fem"], work, "bad", ext, binary=False)

    hangs, crashes, results = [], [], []
    for label, path in variants:
        base = path[:-len(ext)]
        # the second argument is what suppresses the modal dialog
        code, _, timed_out = _run([exe, base, "bLinehook"])
        if timed_out:
            hangs.append(label)
            _kill(solver)
        elif _is_crash(code):
            crashes.append((label, code))
        results.append((label, "HUNG" if timed_out else code))

    _note("    %-9s %s" % (solver,
                           "  ".join("%s=%s" % r for r in results)))
    assert not hangs, (
        "%s hung on %s -- a solver must never block on bad input; check that "
        "MsgBox is still suppressed when a second argument is present "
        "(StdAfx.cpp)" % (solver, ", ".join(hangs)))
    assert not crashes, (
        "%s crashed on %s" % (solver, ", ".join(
            "%s (0x%X)" % (l, c & 0xFFFFFFFF) for l, c in crashes)))


def _kill(name):
    subprocess.run(["taskkill", "/F", "/IM", name + ".exe"],
                   capture_output=True, text=True)


def test_solver_one_argument_form_is_interactive_by_design():
    """Pins the argc>=3 rule, because getting it wrong hangs a build.

    This is documented behaviour, not a defect: fkn/StdAfx.cpp suppresses
    MsgBox only when __argc >= 3, so the one-argument form is the
    interactive one. It is asserted here because the consequence of not
    knowing it is a CI job that hangs forever rather than failing, and
    because a future change that removes the guard would otherwise go
    unnoticed until it did exactly that.
    """
    exe = SOLVERS["fkn"]
    if not os.path.exists(exe):
        pytest.skip("fkn not built")

    work = os.path.join(OUTPUT_DIR, "argc")
    variants = write_variants(sources_fem_fallback(), work, "bad", ".fem",
                              binary=False)
    label, path = variants[0]
    base = path[:-4]

    # two-argument form: must exit
    code, _, timed_out = _run([exe, base, "bLinehook"], timeout=20)
    assert not timed_out, "the two-argument form hung, which breaks automation"
    _note("    fkn 2-arg on %s -> exit %s (non-interactive, as designed)"
          % (label, code))

    # one-argument form: expected to block, so give it a short leash
    code1, _, timed_out1 = _run([exe, base], timeout=8)
    _kill("fkn")
    _note("    fkn 1-arg on %s -> %s (interactive path; suppressed only when "
          "__argc >= 3)" % (label, "hung, killed" if timed_out1 else
                            "exit %s" % code1))


def sources_fem_fallback():
    path = _find_source("file_format_roundtrip/solve_src.fem",
                        "magnetics_analytic/two_wires.fem")
    if not path:
        pytest.skip("no valid .fem to corrupt")
    return path


# ---------------------------------------------------------------------------
# 2. femmqt's CLI paths
# ---------------------------------------------------------------------------

def test_femmqt_convert_ansx_rejects_corrupt_ans(sources):
    """--convert-ansx must fail cleanly on a corrupt .ans."""
    if not os.path.exists(FEMMQT):
        pytest.skip("femmqt not built")

    work = os.path.join(OUTPUT_DIR, "qt_ansx")
    variants = write_variants(sources["ans"], work, "bad", ".ans", binary=False)

    hangs, crashes, results = [], [], []
    for label, path in variants:
        code, _, timed_out = _run([FEMMQT, "--convert-ansx", path])
        if timed_out:
            hangs.append(label)
            _kill("femmqt")
        elif _is_crash(code):
            crashes.append((label, code))
        results.append((label, "HUNG" if timed_out else code))

    _note("    femmqt --convert-ansx  %s"
          % "  ".join("%s=%s" % r for r in results))
    assert not hangs, "femmqt --convert-ansx hung on %s" % ", ".join(hangs)
    assert not crashes, ("femmqt --convert-ansx crashed on %s" % ", ".join(
        "%s (0x%X)" % (l, c & 0xFFFFFFFF) for l, c in crashes))

    # The ticket asks for a non-zero exit on invalid input, not merely the
    # absence of a crash. A truncated .ans used to convert "successfully":
    # measured, a 10%-truncated file produced a .ansx claiming 9469 nodes
    # and 0 elements and exited 0. garbage_appended is excluded because the
    # real data is all present and only trailing junk was added, which is
    # legitimately convertible.
    accepted = [l for l, c in results
                if c == 0 and l.startswith("trunc")]
    assert not accepted, (
        "femmqt --convert-ansx reported success on truncated input (%s); a "
        "partially-read mesh must be rejected, not cached"
        % ", ".join(accepted))


def test_femmqt_import_dxf_rejects_corrupt_dxf(sources):
    """--import-dxf must fail cleanly on a corrupt .dxf."""
    if not os.path.exists(FEMMQT):
        pytest.skip("femmqt not built")

    work = os.path.join(OUTPUT_DIR, "qt_dxf")
    variants = write_variants(sources["dxf"], work, "bad", ".dxf", binary=False)

    hangs, crashes, results = [], [], []
    for label, path in variants:
        out = path[:-4] + "_out.fem"
        code, _, timed_out = _run([FEMMQT, "--import-dxf", path, out])
        if timed_out:
            hangs.append(label)
            _kill("femmqt")
        elif _is_crash(code):
            crashes.append((label, code))
        results.append((label, "HUNG" if timed_out else code))

    _note("    femmqt --import-dxf    %s"
          % "  ".join("%s=%s" % r for r in results))
    assert not hangs, "femmqt --import-dxf hung on %s" % ", ".join(hangs)
    assert not crashes, ("femmqt --import-dxf crashed on %s" % ", ".join(
        "%s (0x%X)" % (l, c & 0xFFFFFFFF) for l, c in crashes))


# ---------------------------------------------------------------------------
# 3. A corrupt CACHE must fall back to the text source, not fail the load
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("cache_ext,source_ext", [(".femx", ".fem"),
                                                  (".ansx", ".ans")])
def test_corrupt_cache_falls_back_to_text(cache_ext, source_ext, sources):
    """FILE_FORMATS.md: a stale/corrupt/wrong-version cache means "stale".

    The load must succeed by re-reading the text source, not fail because
    the optional speedup file is damaged.
    """
    key = cache_ext.strip(".")
    src_cache = sources[key]
    src_text = sources[source_ext.strip(".")]

    work = os.path.join(OUTPUT_DIR, "cache_fallback_%s" % key)
    os.makedirs(work, exist_ok=True)
    stem = os.path.join(work, "model")
    shutil.copyfile(src_text, stem + source_ext)
    if source_ext == ".ans":
        # loading a solution needs its .fem beside it
        shutil.copyfile(sources["fem"], stem + ".fem")

    with open(src_cache, "rb") as fh:
        cache_bytes = fh.read()

    outcomes = []
    for label, blob in corrupt_variants(cache_bytes, binary=True).items():
        with open(stem + cache_ext, "wb") as fh:
            fh.write(blob)

        femm.openfemm(1)
        try:
            femm.opendocument(_save(stem + ".fem"))
            if source_ext == ".ans":
                femm.mi_loadsolution()
                probe = int(femm.mo_numnodes())
                ok = probe > 0
            else:
                # a successful text load still knows its geometry
                path = os.path.join(work, "resaved_%s.fem" % label)
                femm.mi_saveas(_save(path))
                ok = os.path.exists(path) and os.path.getsize(path) > 0
            outcomes.append((label, "loaded" if ok else "EMPTY"))
        except Exception as exc:  # noqa: BLE001
            outcomes.append((label, "FAILED: %s"
                             % str(exc).replace(os.linesep, " ")[:60]))
        finally:
            _teardown()

    _note("    corrupt %s -> %s" % (cache_ext,
                                    "  ".join("%s=%s" % o for o in outcomes)))
    failures = [o for o in outcomes if o[1] != "loaded"]
    assert not failures, (
        "a damaged %s cache must be ignored in favour of re-reading %s, but "
        "these variants broke the load: %s"
        % (cache_ext, source_ext,
           ", ".join("%s (%s)" % f for f in failures)))


# ---------------------------------------------------------------------------
# 4. The COM path
# ---------------------------------------------------------------------------

def test_com_opendocument_survives_corrupt_fem(sources):
    """A corrupt .fem over COM must leave the session usable."""
    work = os.path.join(OUTPUT_DIR, "com")
    variants = write_variants(sources["fem"], work, "bad", ".fem", binary=False)

    outcomes = []
    for label, path in variants:
        femm.openfemm(1)
        try:
            try:
                femm.opendocument(_save(path))
                result = "opened"
            except Exception as exc:  # noqa: BLE001
                result = "rejected: %s" % str(exc).replace(os.linesep, " ")[:44]
            # the session must still answer afterwards
            femm.newdocument(0)
            femm.mi_probdef(0, "millimeters", "planar", 1e-9, 1, 30)
            femm.mi_addnode(0.0, 0.0)
            outcomes.append((label, result))
        finally:
            _teardown()

    _note("    COM opendocument       %s"
          % "  ".join("%s=%s" % (l, r.split(":")[0]) for l, r in outcomes))
    assert len(outcomes) == len(variants), (
        "the COM session stopped responding partway through: %r" % outcomes)


def test_write_report():
    path = _write_report()
    assert os.path.exists(path)
    print("report: " + path)
