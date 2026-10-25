"""Regenerate the classic block-integral references for tst_magnetics_integrals.

Added by Claude (Anthropic), noreply@anthropic.com, 2026-10-25 (issue #108).

femmqt's Area Properties dialog ports classic's BlockIntegral(), and an
integral that is wrong by a constant still looks like a number. So every
expected value in that test comes from the classic post-processor itself,
run on the same solution file femmqt reads -- not from a re-derivation.

For each magnetics demo this:
  1. copies the demo here and solves it with the COM-registered femmx.exe,
     leaving the .ans beside this script (that is the file femmqt reads);
  2. selects each block set listed below in turn;
  3. records mo_blockintegral(k) for k = 0..24 -- classic's own inttype
     numbering, see femm/FemmviewDoc.cpp's BlockIntegral -- as [re, im].

Run it from anywhere with the classic GUI's COM server registered
(scripts/register_femm_com.ps1). It overwrites reference.json and the
.ans files; the .fem copies are deleted again, since the demos directory
already carries them.
"""

import json
import os
import shutil
import sys

import femm  # PyPI pyfemm, driving femm.ActiveFEMM over COM

HERE = os.path.dirname(os.path.abspath(__file__))
DEMOS = os.path.join(HERE, "..", "..", "..", "demos", "magnetics")

# model -> named selections, each a list of block-label points (x, y)
# taken from the demo's own [BlockLabels] section.
SELECTIONS = {
    "straight_wire": {
        "wire": [(0, 0)],
        "air": [(10, 0)],
    },
    "coax_cable": {
        "inner": [(0, 0)],
        "dielectric": [(3, 0)],
        "outer": [(5.5, 0)],
    },
    "two_wires_force": {
        "left_wire": [(-10, 0)],
        "right_wire": [(10, 0)],
    },
    "saturating_core": {
        "core": [(30, 5)],
        "coil_side": [(15, 30)],
        "window_air": [(30, 30)],
    },
    # AC: the coax again at 10 kHz -- the skin-effect demo's own mesh
    # makes a 13 MB solution, too big to commit.
    "coax_cable_ac": {
        "inner": [(0, 0)],
        "dielectric": [(3, 0)],
        "outer": [(5.5, 0)],
    },
    "solenoid_axi": {
        "coil": [(10.75, 0)],
        "air": [(60, 60)],
    },
}

INTEGRALS = range(0, 25)

# Models that are a demo re-solved at another frequency: name -> (demo, Hz).
VARIANTS = {"coax_cable_ac": ("coax_cable", 10000)}


def main():
    femm.openfemm(1)
    femm.main_minimize()
    out = {}
    try:
        for model, selections in SELECTIONS.items():
            demo, hz = VARIANTS.get(model, (model, None))
            fem = os.path.join(HERE, model + ".fem")
            shutil.copyfile(os.path.join(DEMOS, demo + ".fem"), fem)
            femm.opendocument(fem)
            if hz is not None:
                femm.mi_probdef(hz, "millimeters", "planar", 1e-8, 1, 30)
            femm.mi_saveas(fem)
            femm.mi_analyze(1)
            femm.mi_loadsolution()
            out[model] = {}
            for name, points in selections.items():
                femm.mo_clearblock()
                for x, y in points:
                    femm.mo_selectblock(x, y)
                values = {}
                for k in INTEGRALS:
                    z = complex(femm.mo_blockintegral(k))
                    values[str(k)] = [z.real, z.imag]
                out[model][name] = {"points": points, "integrals": values}
                print(model, name, "area", values["5"][0], file=sys.stderr)
            femm.mo_close()
            femm.mi_close()
            # Only the .ans is a fixture; the .fem is the demo's, and the
            # .femx/.ansx are caches femmqt regenerates.
            for ext in (".fem", ".femx", ".ansx"):
                path = os.path.join(HERE, model + ext)
                if os.path.exists(path):
                    os.remove(path)
    finally:
        femm.closefemm()

    with open(os.path.join(HERE, "reference.json"), "w") as f:
        json.dump(out, f, indent=1, sort_keys=True)
        f.write("\n")


if __name__ == "__main__":
    main()
