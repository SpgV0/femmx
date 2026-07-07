"""
Demo: scripted FEMM magnetostatics model of a long straight current-carrying wire.

Builds a 2D planar magnetics problem in FEMM entirely from Python (via the
pyfemm COM interface to femm.exe), solves it, and checks the computed flux
density against the closed-form solution for an infinite straight wire:

    B(r) = mu0 * I / (2 * pi * r)

Requirements:
    - Windows, with a built femm.exe registered as an OLE Automation server
      (running femm.exe once is enough for it to self-register).
    - pip install pyfemm pywin32

Usage:
    python straight_wire_field.py
"""

import math
import os

import femm

MU0 = 4 * math.pi * 1e-7  # H/m

WIRE_RADIUS_MM = 1.0
DOMAIN_RADIUS_MM = 20.0
ABC_LAYERS = 7
CURRENT_A = 10.0
PROBE_RADIUS_MM = 5.0

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "results", "straight_wire_field")
MODEL_PATH = os.path.join(OUTPUT_DIR, "straight_wire_field.fem")


def build_and_solve():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    femm.openfemm()
    femm.newdocument(0)  # 0 = magnetics problem

    # frequency=0 (magnetostatic), units=millimeters, type=planar,
    # precision=1e-8, depth=1, minangle=30
    femm.mi_probdef(0, "millimeters", "planar", 1e-8, 1, 30)

    femm.mi_getmaterial("Air")
    femm.mi_getmaterial("Copper")
    femm.mi_addcircprop("icoil", CURRENT_A, 1)  # 1 = series circuit

    # Wire cross-section: a small circle at the origin, built from two arcs.
    femm.mi_addnode(WIRE_RADIUS_MM, 0)
    femm.mi_addnode(-WIRE_RADIUS_MM, 0)
    femm.mi_addarc(WIRE_RADIUS_MM, 0, -WIRE_RADIUS_MM, 0, 180, 2.5)
    femm.mi_addarc(-WIRE_RADIUS_MM, 0, WIRE_RADIUS_MM, 0, 180, 2.5)
    femm.mi_addblocklabel(0, 0)
    femm.mi_selectlabel(0, 0)
    femm.mi_setblockprop("Copper", 1, 0, "icoil", 0, 0, 1)
    femm.mi_clearselected()

    # Modeled air domain surrounding the wire.
    femm.mi_addnode(DOMAIN_RADIUS_MM, 0)
    femm.mi_addnode(-DOMAIN_RADIUS_MM, 0)
    femm.mi_addarc(DOMAIN_RADIUS_MM, 0, -DOMAIN_RADIUS_MM, 0, 180, 2.5)
    femm.mi_addarc(-DOMAIN_RADIUS_MM, 0, DOMAIN_RADIUS_MM, 0, 180, 2.5)
    femm.mi_addblocklabel(DOMAIN_RADIUS_MM / 2, 0)
    femm.mi_selectlabel(DOMAIN_RADIUS_MM / 2, 0)
    femm.mi_setblockprop("Air", 1, 0, "", 0, 0, 0)
    femm.mi_clearselected()

    # Open (unbounded) boundary: concentric shells extending to infinity,
    # anchored at the outer edge of the domain drawn above.
    femm.mi_makeABC(ABC_LAYERS, DOMAIN_RADIUS_MM, 0, 0, 0)

    femm.mi_zoomnatural()
    femm.mi_saveas(MODEL_PATH)
    femm.mi_analyze(1)
    femm.mi_loadsolution()


def check_result():
    bx, by = femm.mo_getb(PROBE_RADIUS_MM, 0)
    b_fem = math.hypot(bx, by)

    r_m = PROBE_RADIUS_MM / 1000.0
    b_analytical = MU0 * CURRENT_A / (2 * math.pi * r_m)

    error_pct = abs(b_fem - b_analytical) / b_analytical * 100

    print(f"Probe point:        r = {PROBE_RADIUS_MM} mm")
    print(f"FEMM result:        |B| = {b_fem:.6e} T  (Bx={bx:.3e}, By={by:.3e})")
    print(f"Analytical (Ampere): B  = {b_analytical:.6e} T")
    print(f"Relative error:      {error_pct:.2f} %")

    return error_pct


def main():
    build_and_solve()
    try:
        error_pct = check_result()
    finally:
        femm.mo_close()
        femm.closefemm()

    if error_pct < 2.0:
        print("PASS: FEMM result matches the analytical solution.")
    else:
        print("FAIL: FEMM result deviates from the analytical solution.")
        raise SystemExit(1)


if __name__ == "__main__":
    main()
