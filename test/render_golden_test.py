"""
Golden-image tests for `femmqt --render-png` (issue #14).

That CLI renders geometry and solutions offscreen with no desktop, which
makes it by far the cheapest way to regression-test the whole render
pipeline. It is also the path the classic GUI's mi_savepng/mo_savepng
shell out to after setgui("qt").

It is demonstrably fragile. `--density` was documented and implemented,
but nothing in main() ever checked the argument, so EVERY --density
render silently produced a contour plot instead -- for multiple releases,
until it was noticed by accident while verifying something unrelated. The
comment recording that is still in main.cpp. A golden-image test would
have caught it the day it shipped, which is the entire argument for this
file.

Comparison is perceptual, never byte-exact: font hinting, driver and
Qt-version differences move individual pixels without changing what the
picture shows. A render must stay within BOTH a per-pixel channel
tolerance and a budget for how many pixels may differ at all.

Updating the references is deliberate, never automatic:

    pytest test/render_golden_test.py --update-goldens

Run that only when a visual change is intended, and commit the changed
PNGs as part of the same change, so an intentional difference is visible
in review.

Requirements: a built femmqt.exe; pip install pillow.

Usage:
    pytest test/render_golden_test.py -v
"""

import os
import subprocess

import pytest

import femmx_paths

try:
    from PIL import Image
except ImportError:  # pragma: no cover - reported as a skip below
    Image = None

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)

FIXTURE_DIR = os.path.join(SCRIPT_DIR, "fixtures", "render")
GOLDEN_DIR = os.path.join(FIXTURE_DIR, "golden")
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "results", "render_golden")

FEMMQT = os.path.join(femmx_paths.BIN_DIR, "femmqt.exe")
MODEL_FEM = os.path.join(FIXTURE_DIR, "render_fixture.fem")
MODEL_ANS = os.path.join(FIXTURE_DIR, "render_fixture.ans")

# Fixed, modest size: big enough to show structure, small enough that the
# committed reference PNGs stay a few tens of KB.
RENDER_W = 400
RENDER_H = 300

# A pixel counts as different if any channel moves by more than this...
CHANNEL_TOL = 24
# ...and at most this fraction of pixels may differ at all.
MAX_DIFF_FRACTION = 0.02

_REPORT = []


def _note(text):
    print(text)
    _REPORT.append(text)


def _write_report():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    path = os.path.join(OUTPUT_DIR, "render_golden.txt")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("--render-png golden image comparison" + os.linesep)
        fh.write("=" * 70 + os.linesep)
        for line in _REPORT:
            fh.write(line + os.linesep)
    return path


@pytest.fixture(scope="module", autouse=True)
def _requirements():
    if Image is None:
        pytest.skip("Pillow is not installed (pip install pillow)")
    if not os.path.exists(FEMMQT):
        pytest.skip("femmqt.exe not built at %s" % FEMMQT)
    for path in (MODEL_FEM, MODEL_ANS):
        assert os.path.exists(path), "missing committed fixture %s" % path
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    os.makedirs(GOLDEN_DIR, exist_ok=True)


# ---------------------------------------------------------------------------
# Rendering and comparing
# ---------------------------------------------------------------------------

def render(source, out_path, extra=(), w=RENDER_W, h=RENDER_H, timeout=120):
    """Invoke the CLI; returns (exit code, combined output)."""
    if os.path.exists(out_path):
        os.remove(out_path)
    cmd = [FEMMQT, "--render-png", source, out_path, str(w), str(h)]
    cmd.extend(str(x) for x in extra)
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        subprocess.run(["taskkill", "/F", "/IM", "femmqt.exe"],
                       capture_output=True, text=True)
        return None, "TIMED OUT"
    return proc.returncode, (proc.stderr or "") + (proc.stdout or "")


def compare(actual_path, golden_path, label):
    """Perceptual comparison. Returns (differing_fraction, worst_channel_delta)."""
    actual = Image.open(actual_path).convert("RGB")
    golden = Image.open(golden_path).convert("RGB")
    assert actual.size == golden.size, (
        "%s: size changed, %r vs golden %r" % (label, actual.size, golden.size))

    a = actual.load()
    g = golden.load()
    width, height = actual.size
    differing = 0
    worst = 0
    diff = Image.new("RGB", actual.size, (0, 0, 0))
    d = diff.load()

    for y in range(height):
        for x in range(width):
            pa, pg = a[x, y], g[x, y]
            delta = max(abs(pa[0] - pg[0]), abs(pa[1] - pg[1]), abs(pa[2] - pg[2]))
            if delta > worst:
                worst = delta
            if delta > CHANNEL_TOL:
                differing += 1
                # red where it differs, scaled by how badly
                d[x, y] = (255, max(0, 255 - delta), max(0, 255 - delta))

    fraction = differing / float(width * height)
    if fraction > MAX_DIFF_FRACTION:
        # leave the evidence behind for CI artifacts
        actual.save(os.path.join(OUTPUT_DIR, "%s_actual.png" % label))
        golden.save(os.path.join(OUTPUT_DIR, "%s_golden.png" % label))
        diff.save(os.path.join(OUTPUT_DIR, "%s_diff.png" % label))
    return fraction, worst


def check_render(request, label, source, extra=()):
    """Render one mode and compare it to its golden, or update it."""
    actual = os.path.join(OUTPUT_DIR, "%s.png" % label)
    golden = os.path.join(GOLDEN_DIR, "%s.png" % label)

    code, output = render(source, actual, extra)
    assert code == 0, "%s: render failed (exit %r): %s" % (label, code, output.strip())
    assert os.path.exists(actual) and os.path.getsize(actual) > 0, (
        "%s: render reported success but wrote no image" % label)

    if request.config.getoption("--update-goldens"):
        Image.open(actual).convert("RGB").save(golden)
        _note("    %-22s GOLDEN UPDATED (%d bytes)"
              % (label, os.path.getsize(golden)))
        return

    assert os.path.exists(golden), (
        "%s: no reference image at %s. Generate it deliberately with "
        "`pytest test/render_golden_test.py --update-goldens` and commit it."
        % (label, golden))

    fraction, worst = compare(actual, golden, label)
    _note("    %-22s %.4f%% of pixels differ, worst channel delta %d "
          "(budget %.2f%% / %d)"
          % (label, fraction * 100.0, worst,
             MAX_DIFF_FRACTION * 100.0, CHANNEL_TOL))
    assert fraction <= MAX_DIFF_FRACTION, (
        "%s: %.3f%% of pixels differ from the reference (budget %.2f%%). "
        "Actual, golden and diff images are in %s. If the change is "
        "intended, re-run with --update-goldens and commit the new PNGs."
        % (label, fraction * 100.0, MAX_DIFF_FRACTION * 100.0, OUTPUT_DIR))


# ---------------------------------------------------------------------------
# One render per mode
# ---------------------------------------------------------------------------

def test_geometry_render(request):
    """The .fem geometry view."""
    check_render(request, "geometry", MODEL_FEM)


def test_contour_render(request):
    """The default solution view, which is contour mode."""
    check_render(request, "contour", MODEL_ANS)


def test_density_render(request):
    """--density must actually produce a density plot.

    This is the exact regression the ticket was written around: --density
    was parsed by nobody, so every such render silently came back as a
    contour plot. The comparison against a density golden catches that,
    and test_density_differs_from_contour below catches it even if both
    goldens were regenerated from a broken build.
    """
    check_render(request, "density", MODEL_ANS, extra=["--density"])


def test_crop_render(request):
    """A scene-space crop.

    Worth its own case because the density plot's colour banding is scaled
    to what is VISIBLE (MeshSolutionItem::paintDensity recomputes min/max
    over elements overlapping the exposed rect), so a full-model render
    cannot stand in for a zoomed one.
    """
    check_render(request, "crop_density", MODEL_ANS,
                 extra=[-20, -15, 20, 15, "--density"])


# ---------------------------------------------------------------------------
# Mode-confusion guard, independent of the goldens
# ---------------------------------------------------------------------------

def test_density_differs_from_contour(request):
    """A density render must not be pixel-identical to a contour one.

    Deliberately independent of the reference images: if --density were
    ignored again AND the goldens were regenerated from that broken build,
    every comparison above would pass while both files showed a contour
    plot. This compares the two modes against each other instead.
    """
    contour = os.path.join(OUTPUT_DIR, "modecheck_contour.png")
    density = os.path.join(OUTPUT_DIR, "modecheck_density.png")

    code_c, out_c = render(MODEL_ANS, contour)
    assert code_c == 0, out_c.strip()
    code_d, out_d = render(MODEL_ANS, density, extra=["--density"])
    assert code_d == 0, out_d.strip()

    a = Image.open(contour).convert("RGB").load()
    b = Image.open(density).convert("RGB").load()
    differing = 0
    for y in range(RENDER_H):
        for x in range(RENDER_W):
            if a[x, y] != b[x, y]:
                differing += 1
    fraction = differing / float(RENDER_W * RENDER_H)

    _note("    %-22s %.2f%% of pixels differ between the two modes"
          % ("density vs contour", fraction * 100.0))
    assert fraction > 0.05, (
        "a --density render is %.3f%% different from a contour render of the "
        "same solution. They should look substantially different; this is "
        "what silently regressed once already, when nothing in main() "
        "checked the --density argument." % (fraction * 100.0))


# ---------------------------------------------------------------------------
# Error paths
# ---------------------------------------------------------------------------

def test_bad_size_is_rejected():
    out = os.path.join(OUTPUT_DIR, "badsize.png")
    code, output = render(MODEL_FEM, out, w=0, h=300)
    _note("    bad size 0x300        exit=%r" % code)
    assert code not in (0, None), (
        "a zero width was accepted (exit %r); output: %s" % (code, output.strip()))
    assert not os.path.exists(out) or os.path.getsize(out) == 0


def test_missing_input_is_rejected():
    out = os.path.join(OUTPUT_DIR, "missing.png")
    code, output = render(os.path.join(FIXTURE_DIR, "no_such_model.fem"), out)
    _note("    missing input         exit=%r" % code)
    assert code not in (0, None), (
        "a missing input file was accepted (exit %r)" % code)


def test_unwritable_output_is_rejected():
    # A path whose parent directory does not exist.
    out = os.path.join(OUTPUT_DIR, "no_such_dir", "nested", "out.png")
    code, output = render(MODEL_FEM, out)
    _note("    unwritable output     exit=%r" % code)
    assert code not in (0, None), (
        "an unwritable output path was accepted (exit %r): %s"
        % (code, output.strip()))


def test_write_report():
    path = _write_report()
    assert os.path.exists(path)
    print("report: " + path)
