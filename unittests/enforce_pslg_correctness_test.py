"""
Correctness test for the incremental CFemmeDoc::EnforcePSLG(tol, nodeStart,
lineStart, arcStart, blockStart) overload (femm/MOVECOPY.CPP).

RotateCopy/TranslateCopy now only re-validate the newly appended geometry
against the *entire* drawing, instead of tearing down and rebuilding
everything. This test checks that copying a line segment across a
pre-existing one still triggers the expected intersection split -- i.e.
that pre-existing geometry is still correctly checked against new geometry,
even though it's no longer being torn down and re-inserted itself.

Setup:
  - An "old" horizontal line from (-10,0) to (10,0).
  - A "to be copied" vertical line from (5,-5) to (5,-1), initially not
    touching the horizontal line.
Action:
  - Copy the vertical line by (dx=0, dy=4). Copy leaves the original
    vertical line in place and adds a new one spanning (5,-1) to (5,3),
    which crosses the horizontal line at (5,0). The new copy's bottom
    endpoint (5,-1) exactly coincides with the original vertical line's
    top endpoint.
Expected result (a valid planar straight-line graph):
  - 6 nodes: (-10,0), (10,0), (5,-5), (5,-1) [shared by the original
    line's top endpoint and the new copy's bottom endpoint -- these must
    be merged into a single node, not duplicated], (5,3), and the
    intersection node created at (5,0).
  - 5 segments: the old horizontal line split into 2 at the intersection,
    the untouched original vertical line (5,-5)-(5,-1) (still 1 segment),
    and the new copied line split into 2 at the intersection.

Requirements: pip install pyfemm pywin32; a built + COM-registered femm.exe.

Usage:
    pytest enforce_pslg_correctness_test.py -v
    python enforce_pslg_correctness_test.py
"""

import os
import re

import pytest

import femm

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
RESULTS_DIR = os.path.join(SCRIPT_DIR, "results", "enforce_pslg_correctness_test")
RESULTS_PATH = os.path.join(RESULTS_DIR, "enforce_pslg_correctness.txt")
MODEL_PATH = os.path.join(RESULTS_DIR, "enforce_pslg_correctness_test.fem")

EXPECTED_COUNTS = {
    "NumPoints": 6,
    "NumSegments": 5,
    "NumArcSegments": 0,
    "NumBlockLabels": 0,
}


def build_and_copy():
    os.makedirs(RESULTS_DIR, exist_ok=True)
    femm.openfemm()
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-8, 1, 30)

    # old, pre-existing horizontal line
    femm.mi_addnode(-10, 0)
    femm.mi_addnode(10, 0)
    femm.mi_addsegment(-10, 0, 10, 0)

    # line to be copied -- does not yet touch the horizontal line
    femm.mi_addnode(5, -5)
    femm.mi_addnode(5, -1)
    femm.mi_addsegment(5, -5, 5, -1)

    # select just the to-be-copied line and copy it up so it crosses
    # the horizontal line at (5,0)
    femm.mi_selectsegment(5, -3)
    femm.mi_copytranslate2(0, 4, 1, 1)
    femm.mi_clearselected()

    femm.mi_saveas(MODEL_PATH)


def read_counts(fem_path):
    with open(fem_path, "r") as f:
        text = f.read()
    counts = {}
    for key in EXPECTED_COUNTS:
        m = re.search(rf"\[{key}\]\s*=\s*(\d+)", text)
        counts[key] = int(m.group(1))
    return counts


@pytest.fixture(scope="module")
def pslg_counts():
    try:
        build_and_copy()
    finally:
        femm.closefemm()

    counts = read_counts(MODEL_PATH)

    lines = [
        "EnforcePSLG incremental-rebuild correctness test",
        "==================================================",
        "Scenario: copy a line segment so it crosses a pre-existing one; "
        "the intersection must still be detected and both lines split.",
        "",
    ]
    for name, expected in EXPECTED_COUNTS.items():
        actual = counts[name]
        ok = actual == expected
        lines.append(f"{name}: expected {expected}, got {actual}  [{'PASS' if ok else 'FAIL'}]")
    report = "\n".join(lines)

    os.makedirs(RESULTS_DIR, exist_ok=True)
    with open(RESULTS_PATH, "w") as f:
        f.write(report + "\n")

    return counts


@pytest.mark.parametrize("key", list(EXPECTED_COUNTS))
def test_pslg_count_after_crossing_copy(pslg_counts, key):
    expected = EXPECTED_COUNTS[key]
    actual = pslg_counts[key]
    assert actual == expected, (
        f"{key}: expected {expected} after copying a segment across a "
        f"pre-existing one, got {actual} -- EnforcePSLG's incremental "
        f"intersection-checking may be broken"
    )


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__, "-v"]))
