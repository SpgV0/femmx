"""
Analytic correctness tests for the magnetics solver (fkn), beyond the
single straight-wire check.

straight_wire_field_test.py asserts one number: |B| at a probe point near
an infinite straight wire -- planar, DC, no materials, no circuits. That
leaves the parts of the magnetics path this fork actually touches almost
entirely unverified numerically: the whole problemtype("axi") code path,
circuit/flux-linkage reporting, the force integrals, BH-curve
interpolation, and the time-harmonic (AC) solve (issue #2).

Each case builds a model from Python via pyfemm, solves it, and compares
against a closed form. Tolerances are stated per case rather than shared,
because they are not all equally tight: a probe on the axis of a
well-resolved coil is good to a fraction of a percent, while anything
involving a saturating material or a meshed exponential decay carries real
discretisation error and says so.

Requirements: a built, COM-registered femmx.exe; pip install pyfemm pywin32.

Usage:
    pytest test/magnetics_analytic_test.py -v
"""

import math
import os

import pytest

import femm

MU0 = 4.0 * math.pi * 1e-7      # H/m

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "results", "magnetics_analytic")

_REPORT = []


def _mm(x):
    """millimetres -> metres."""
    return x / 1000.0


def _re(value, tol=1e-6):
    """Real part of a possibly-complex FEMM result (AC solves return phasors)."""
    if isinstance(value, complex):
        mag = abs(value)
        if mag and abs(value.imag) / mag > tol:
            raise AssertionError("expected a real result, got %r" % (value,))
        return value.real
    return value


def _record(name, quantity, fem_value, analytic, unit, tol_pct):
    err = abs(fem_value - analytic) / abs(analytic) * 100.0 if analytic else float("inf")
    line = ("%-26s %-24s FEM=%12.6g  analytic=%12.6g %-7s err=%6.2f %% (tol %.1f)"
            % (name, quantity, fem_value, analytic, unit, err, tol_pct))
    print(line)
    _REPORT.append(line)
    return err


def _note(text):
    print(text)
    _REPORT.append(text)


def _save(path):
    return path.replace(chr(92), "/")


def _teardown():
    """Close the post-processor and the session, tolerantly.

    All fixtures here are module-scoped and tear down together at the end;
    the first closefemm() clears pyfemm's module-level HandleToFEMM, so
    every later teardown would otherwise die with a NameError that says
    nothing about the physics under test.
    """
    for fn in (femm.mo_close, femm.closefemm):
        try:
            fn()
        except Exception:  # noqa: BLE001 -- teardown must not mask results
            pass


def _circle(add_node, add_arc, cx, cy, r, maxseg=1.0):
    """Draw a full circle as two 180-degree arcs."""
    add_node(cx + r, cy)
    add_node(cx - r, cy)
    add_arc(cx + r, cy, cx - r, cy, 180, maxseg)
    add_arc(cx - r, cy, cx + r, cy, 180, maxseg)


def _rect(add_node, add_seg, x0, y0, x1, y1):
    for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        add_node(x, y)
    add_seg(x0, y0, x1, y0)
    add_seg(x1, y0, x1, y1)
    add_seg(x1, y1, x0, y1)
    add_seg(x0, y1, x0, y0)


def _write_report():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    path = os.path.join(OUTPUT_DIR, "magnetics_analytic.txt")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("Analytic correctness checks for the magnetics solver (fkn)" + os.linesep)
        fh.write("=" * 78 + os.linesep)
        for line in _REPORT:
            fh.write(line + os.linesep)
    return path


# ---------------------------------------------------------------------------
# 1. Axisymmetric: single circular loop
#
#   B_z on the axis at the plane of the loop:   B = mu0 * I / (2a)
#
# This is the first numeric check of the problemtype("axi") code path at all.
# ---------------------------------------------------------------------------

LOOP_A_MM = 20.0        # loop radius
LOOP_RW_MM = 0.6        # conductor radius
LOOP_I_A = 10.0
LOOP_DOMAIN_MM = 120.0
LOOP_TOL_PCT = 2.0


def _build_loop():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    femm.openfemm(1)
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "axi", 1e-9, 0, 30)

    femm.mi_getmaterial("Air")
    femm.mi_getmaterial("Copper")
    femm.mi_addcircprop("loop", LOOP_I_A, 1)

    _circle(femm.mi_addnode, femm.mi_addarc, LOOP_A_MM, 0.0, LOOP_RW_MM, 1.0)
    femm.mi_addblocklabel(LOOP_A_MM, 0.0)
    femm.mi_selectlabel(LOOP_A_MM, 0.0)
    femm.mi_setblockprop("Copper", 1, 0, "loop", 0, 0, 1)
    femm.mi_clearselected()

    # Air half-disc in the r>=0 half plane, closed by the axis.
    femm.mi_addnode(0, LOOP_DOMAIN_MM)
    femm.mi_addnode(0, -LOOP_DOMAIN_MM)
    femm.mi_addarc(0, -LOOP_DOMAIN_MM, 0, LOOP_DOMAIN_MM, 180, 2.5)
    femm.mi_addsegment(0, -LOOP_DOMAIN_MM, 0, LOOP_DOMAIN_MM)
    femm.mi_addblocklabel(LOOP_DOMAIN_MM / 2.0, LOOP_DOMAIN_MM / 2.0)
    femm.mi_selectlabel(LOOP_DOMAIN_MM / 2.0, LOOP_DOMAIN_MM / 2.0)
    femm.mi_setblockprop("Air", 1, 0, "", 0, 0, 0)
    femm.mi_clearselected()

    femm.mi_makeABC(7, LOOP_DOMAIN_MM, 0, 0, 0)
    femm.mi_zoomnatural()
    femm.mi_saveas(_save(os.path.join(OUTPUT_DIR, "loop.fem")))
    femm.mi_analyze(1)
    femm.mi_loadsolution()


@pytest.fixture(scope="module")
def loop():
    _build_loop()
    try:
        yield
    finally:
        _teardown()


def test_axisymmetric_loop_on_axis_field(loop):
    """B on the axis of a circular loop must equal mu0*I/(2a)."""
    bx, by = femm.mo_getb(0.0, 0.0)
    b_fem = abs(_re(by))          # on the axis the field is purely axial
    b_exact = MU0 * LOOP_I_A / (2.0 * _mm(LOOP_A_MM))
    err = _record("axi loop", "B on axis", b_fem, b_exact, "T", LOOP_TOL_PCT)
    assert err < LOOP_TOL_PCT


def test_axisymmetric_loop_off_axis_falls_as_cube(loop):
    """Far from the loop, B on the axis must fall as the dipole 1/z^3."""
    z_far = 90.0
    b_far = abs(_re(femm.mo_getb(0.0, z_far)[1]))
    a = _mm(LOOP_A_MM)
    z = _mm(z_far)
    b_exact = MU0 * LOOP_I_A * a * a / (2.0 * (a * a + z * z) ** 1.5)
    err = _record("axi loop", "B on axis z=90mm", b_far, b_exact, "T", 5.0)
    assert err < 5.0


# ---------------------------------------------------------------------------
# 2. Coaxial cable: external inductance from stored field energy
#
#   L_ext per unit length = mu0/(2*pi) * ln(b/a)
#
# The energy integral is taken over the air annulus only, which isolates the
# external inductance and side-steps the internal-inductance term the solid
# inner conductor would otherwise contribute (mu0/(8*pi) per metre).
# ---------------------------------------------------------------------------

COAX_A_MM = 1.0
COAX_B_MM = 5.0
COAX_SHELL_MM = 6.0
COAX_I_A = 20.0
COAX_DEPTH_MM = 100.0
COAX_TOL_PCT = 3.0


def _build_coax_inductance():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    femm.openfemm(1)
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-9, COAX_DEPTH_MM, 30)

    femm.mi_getmaterial("Air")
    femm.mi_getmaterial("Copper")
    femm.mi_addcircprop("go", COAX_I_A, 1)
    femm.mi_addcircprop("ret", -COAX_I_A, 1)

    _circle(femm.mi_addnode, femm.mi_addarc, 0, 0, COAX_A_MM, 1.0)
    _circle(femm.mi_addnode, femm.mi_addarc, 0, 0, COAX_B_MM, 1.0)
    _circle(femm.mi_addnode, femm.mi_addarc, 0, 0, COAX_SHELL_MM, 1.0)

    femm.mi_addblocklabel(0, 0)                       # inner conductor
    femm.mi_selectlabel(0, 0)
    femm.mi_setblockprop("Copper", 1, 0, "go", 0, 0, 1)
    femm.mi_clearselected()

    r_air = (COAX_A_MM + COAX_B_MM) / 2.0             # air annulus
    femm.mi_addblocklabel(r_air, 0)
    femm.mi_selectlabel(r_air, 0)
    femm.mi_setblockprop("Air", 1, 0, "", 0, 0, 0)
    femm.mi_clearselected()

    r_shell = (COAX_B_MM + COAX_SHELL_MM) / 2.0       # return shell
    femm.mi_addblocklabel(r_shell, 0)
    femm.mi_selectlabel(r_shell, 0)
    femm.mi_setblockprop("Copper", 1, 0, "ret", 0, 0, 1)
    femm.mi_clearselected()

    femm.mi_zoomnatural()
    femm.mi_saveas(_save(os.path.join(OUTPUT_DIR, "coax_inductance.fem")))
    femm.mi_analyze(1)
    femm.mi_loadsolution()


@pytest.fixture(scope="module")
def coax_inductance():
    _build_coax_inductance()
    try:
        yield
    finally:
        _teardown()


def test_coax_external_inductance(coax_inductance):
    """Field energy in the annulus must equal 0.5*L_ext*I^2."""
    r_air = (COAX_A_MM + COAX_B_MM) / 2.0
    femm.mo_selectblock(r_air, 0)
    energy = _re(femm.mo_blockintegral(2))    # 2 = magnetic field energy, J
    femm.mo_clearblock()

    l_fem = 2.0 * energy / (COAX_I_A ** 2)
    l_exact = (MU0 / (2.0 * math.pi)) * math.log(COAX_B_MM / COAX_A_MM) * _mm(COAX_DEPTH_MM)
    err = _record("coax", "L_ext from energy", l_fem, l_exact, "H", COAX_TOL_PCT)
    assert err < COAX_TOL_PCT


def test_coax_field_between_conductors(coax_inductance):
    """Ampere's law inside the annulus: B = mu0*I/(2*pi*r)."""
    worst = 0.0
    for r in (2.0, 3.0, 4.0):
        b = abs(_re(femm.mo_getb(r, 0)[1]))
        b_exact = MU0 * COAX_I_A / (2.0 * math.pi * _mm(r))
        worst = max(worst, _record("coax", "B at r=%.1f mm" % r, b, b_exact,
                                   "T", COAX_TOL_PCT))
    assert worst < COAX_TOL_PCT


# ---------------------------------------------------------------------------
# 3. Finite solenoid, on-axis field
#
#   B(centre) = mu0 * N * I / sqrt(L^2 + 4R^2)
#
# Two length/diameter ratios, so the end effects the finite form accounts
# for are actually exercised rather than sitting in the infinite limit.
# ---------------------------------------------------------------------------

SOL_R_MM = 10.0
SOL_T_MM = 1.5          # winding thickness (kept thin: the closed form is a current sheet)
SOL_TURNS = 200
SOL_I_A = 2.0
SOL_TOL_PCT = 3.0


def _build_solenoid(length_mm, tag):
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    femm.openfemm(1)
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "axi", 1e-9, 0, 30)

    femm.mi_getmaterial("Air")
    femm.mi_getmaterial("Copper")
    femm.mi_addcircprop("coil", SOL_I_A, 1)

    half = length_mm / 2.0
    _rect(femm.mi_addnode, femm.mi_addsegment,
          SOL_R_MM, -half, SOL_R_MM + SOL_T_MM, half)
    mid = (SOL_R_MM + SOL_T_MM / 2.0, 0.0)
    femm.mi_addblocklabel(*mid)
    femm.mi_selectlabel(*mid)
    # turns = SOL_TURNS in this block
    femm.mi_setblockprop("Copper", 1, 0, "coil", 0, 0, SOL_TURNS)
    femm.mi_clearselected()

    domain = max(120.0, length_mm * 3.0)
    femm.mi_addnode(0, domain)
    femm.mi_addnode(0, -domain)
    femm.mi_addarc(0, -domain, 0, domain, 180, 2.5)
    femm.mi_addsegment(0, -domain, 0, domain)
    femm.mi_addblocklabel(domain / 2.0, domain / 2.0)
    femm.mi_selectlabel(domain / 2.0, domain / 2.0)
    femm.mi_setblockprop("Air", 1, 0, "", 0, 0, 0)
    femm.mi_clearselected()

    femm.mi_makeABC(7, domain, 0, 0, 0)
    femm.mi_zoomnatural()
    femm.mi_saveas(_save(os.path.join(OUTPUT_DIR, "solenoid_%s.fem" % tag)))
    femm.mi_analyze(1)
    femm.mi_loadsolution()


def _solenoid_centre_field(length_mm):
    r = _mm(SOL_R_MM + SOL_T_MM / 2.0)
    l = _mm(length_mm)
    return MU0 * SOL_TURNS * SOL_I_A / math.sqrt(l * l + 4.0 * r * r)


@pytest.mark.parametrize("length_mm,tag", [(60.0, "long"), (20.0, "short")])
def test_solenoid_centre_field(length_mm, tag):
    """On-axis centre field vs the finite-solenoid closed form."""
    _build_solenoid(length_mm, tag)
    try:
        b_fem = abs(_re(femm.mo_getb(0.0, 0.0)[1]))
        b_exact = _solenoid_centre_field(length_mm)
        err = _record("solenoid L=%gmm" % length_mm, "B on axis, centre",
                      b_fem, b_exact, "T", SOL_TOL_PCT)
        assert err < SOL_TOL_PCT
    finally:
        _teardown()


# ---------------------------------------------------------------------------
# 4. Force: two parallel conductors
#
#   F/L = mu0 * I1 * I2 / (2*pi*d)     (attractive for parallel currents)
#
# Deliberately NOT an iron air gap. A gap force can only be compared against
# B^2*A/(2*mu0) once fringing and the meshing of the gap are accounted for,
# so it tests the discretisation as much as the force integral. Two wires in
# air have an exact closed form, and they let the two force routes FEMM
# offers be cross-checked against each other on the same model, which is
# what the ticket actually asks for:
#
#   block integral 11/12  steady-state Lorentz force
#   block integral 18/19  steady-state weighted stress tensor force
# ---------------------------------------------------------------------------

WIRE_R_MM = 1.0
WIRE_SEP_MM = 20.0
WIRE_I_A = 50.0
WIRE_DEPTH_MM = 1000.0      # 1 m, so the integral is directly force per metre
WIRE_DOMAIN_MM = 200.0
WIRE_TOL_PCT = 5.0
WIRE_CROSSCHECK_PCT = 5.0


def _build_two_wires():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    femm.openfemm(1)
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-9, WIRE_DEPTH_MM, 30)

    femm.mi_getmaterial("Air")
    femm.mi_getmaterial("Copper")
    femm.mi_addcircprop("i1", WIRE_I_A, 1)
    femm.mi_addcircprop("i2", WIRE_I_A, 1)

    x1 = -WIRE_SEP_MM / 2.0
    x2 = WIRE_SEP_MM / 2.0
    _circle(femm.mi_addnode, femm.mi_addarc, x1, 0, WIRE_R_MM, 0.5)
    _circle(femm.mi_addnode, femm.mi_addarc, x2, 0, WIRE_R_MM, 0.5)

    femm.mi_addblocklabel(x1, 0)
    femm.mi_selectlabel(x1, 0)
    femm.mi_setblockprop("Copper", 1, 0, "i1", 0, 1, 1)   # group 1
    femm.mi_clearselected()

    femm.mi_addblocklabel(x2, 0)
    femm.mi_selectlabel(x2, 0)
    femm.mi_setblockprop("Copper", 1, 0, "i2", 0, 2, 1)   # group 2
    femm.mi_clearselected()

    _circle(femm.mi_addnode, femm.mi_addarc, 0, 0, WIRE_DOMAIN_MM, 5.0)
    femm.mi_addblocklabel(0, WIRE_DOMAIN_MM / 2.0)
    femm.mi_selectlabel(0, WIRE_DOMAIN_MM / 2.0)
    femm.mi_setblockprop("Air", 1, 0, "", 0, 0, 0)
    femm.mi_clearselected()

    femm.mi_makeABC(7, WIRE_DOMAIN_MM, 0, 0, 0)
    femm.mi_zoomnatural()
    femm.mi_saveas(_save(os.path.join(OUTPUT_DIR, "two_wires.fem")))
    femm.mi_analyze(1)
    femm.mi_loadsolution()


@pytest.fixture(scope="module")
def two_wires():
    _build_two_wires()
    try:
        yield
    finally:
        _teardown()


def _force_per_metre():
    return MU0 * WIRE_I_A * WIRE_I_A / (2.0 * math.pi * _mm(WIRE_SEP_MM))


def test_two_wire_lorentz_force(two_wires):
    """Lorentz force on one conductor vs mu0*I1*I2/(2*pi*d)."""
    femm.mo_groupselectblock(2)
    fx = abs(_re(femm.mo_blockintegral(11)))   # 11 = x part, steady-state Lorentz
    femm.mo_clearblock()

    err = _record("two wires", "F/L Lorentz", fx, _force_per_metre(), "N/m",
                  WIRE_TOL_PCT)
    assert err < WIRE_TOL_PCT


def test_two_wire_weighted_stress_tensor_agrees(two_wires):
    """The WST route must agree with the Lorentz route on the same model."""
    femm.mo_groupselectblock(2)
    f_lorentz = abs(_re(femm.mo_blockintegral(11)))
    femm.mo_clearblock()

    femm.mo_groupselectblock(2)
    f_wst = abs(_re(femm.mo_blockintegral(18)))   # 18 = x part, steady-state WST
    femm.mo_clearblock()

    err = _record("two wires", "F/L WST", f_wst, _force_per_metre(), "N/m",
                  WIRE_TOL_PCT)
    rel = abs(f_wst - f_lorentz) / f_lorentz * 100.0
    _note("    Lorentz vs WST on the same block differ by %.2f %%" % rel)
    assert err < WIRE_TOL_PCT
    assert rel < WIRE_CROSSCHECK_PCT


def test_two_wire_field_at_midpoint(two_wires):
    """Midway between equal parallel currents the fields cancel."""
    bx, by = femm.mo_getb(0.0, 0.0)
    b_mid = math.hypot(_re(bx), _re(by))
    b_single = MU0 * WIRE_I_A / (2.0 * math.pi * _mm(WIRE_SEP_MM / 2.0))
    _note("    |B| at the midpoint = %.3e T, one wire alone would give %.3e T"
          % (b_mid, b_single))
    assert b_mid < b_single * 0.05, (
        "fields should very nearly cancel midway between two equal parallel "
        "currents, got %.3e T against %.3e T for a single wire"
        % (b_mid, b_single))


# ---------------------------------------------------------------------------
# 5. Nonlinear material driven into saturation
#
# A CLOSED planar picture-frame core -- no gap, no free ends -- wound with N
# turns. A closed magnetic circuit is what makes the drive well defined:
# Ampere's law around the mean path gives
#
#   H_mean = N*I / l_mean
#
# with no demagnetising factor to estimate and no gap reluctance to dominate.
# (An axisymmetric model cannot be used here: a toroidal winding threads the
# core around its minor cross-section, which is not a figure of revolution.)
#
# The assertions are about the SHAPE of the response, since the curve is the
# material library's data rather than a formula:
#
#   * at low drive the material sits on its initial-permeability slope, so
#     mu_r is large;
#   * driven hard it must roll onto the knee -- mu_r collapses while H rises;
#   * B must stay physically bounded, which a broken interpolation or an
#     extrapolated table would violate.
#
# This is the check that catches BH-interpolation regressions of the kind
# v2.1.2 shipped, where mu_r was effectively treated as 1 regardless of the
# curve.
# ---------------------------------------------------------------------------

FRAME_W_MM = 60.0
FRAME_H_MM = 60.0
FRAME_T_MM = 10.0        # limb thickness
FRAME_TURNS = 200
FRAME_DEPTH_MM = 20.0
FRAME_MATERIAL = "Pure Iron"


def _frame_mean_path_m():
    """Perimeter of the mean flux path around the frame."""
    return _mm(2.0 * ((FRAME_W_MM - FRAME_T_MM) + (FRAME_H_MM - FRAME_T_MM)))


def _build_frame(current_a, tag):
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    femm.openfemm(1)
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-9, FRAME_DEPTH_MM, 30)

    femm.mi_getmaterial("Air")
    femm.mi_getmaterial("Copper")
    femm.mi_getmaterial(FRAME_MATERIAL)
    femm.mi_addcircprop("go", current_a, 1)
    femm.mi_addcircprop("ret", -current_a, 1)

    w, h, t = FRAME_W_MM, FRAME_H_MM, FRAME_T_MM

    # outer boundary and the window: the iron is what lies between them
    _rect(femm.mi_addnode, femm.mi_addsegment, 0, 0, w, h)
    _rect(femm.mi_addnode, femm.mi_addsegment, t, t, w - t, h - t)

    femm.mi_addblocklabel(w / 2.0, t / 2.0)          # bottom yoke: the iron
    femm.mi_selectlabel(w / 2.0, t / 2.0)
    femm.mi_setblockprop(FRAME_MATERIAL, 1, 0, "", 0, 0, 0)
    femm.mi_clearselected()

    # coil around the left limb: "go" inside the window, "ret" outside the core
    _rect(femm.mi_addnode, femm.mi_addsegment, t + 2.0, h / 2.0 - 8.0,
          t + 8.0, h / 2.0 + 8.0)
    go = (t + 5.0, h / 2.0)
    femm.mi_addblocklabel(*go)
    femm.mi_selectlabel(*go)
    femm.mi_setblockprop("Copper", 1, 0, "go", 0, 0, FRAME_TURNS)
    femm.mi_clearselected()

    _rect(femm.mi_addnode, femm.mi_addsegment, -8.0, h / 2.0 - 8.0,
          -2.0, h / 2.0 + 8.0)
    ret = (-5.0, h / 2.0)
    femm.mi_addblocklabel(*ret)
    femm.mi_selectlabel(*ret)
    femm.mi_setblockprop("Copper", 1, 0, "ret", 0, 0, FRAME_TURNS)
    femm.mi_clearselected()

    # air inside the window, clear of the coil
    femm.mi_addblocklabel(w / 2.0, h / 2.0)
    femm.mi_selectlabel(w / 2.0, h / 2.0)
    femm.mi_setblockprop("Air", 1, 0, "", 0, 0, 0)
    femm.mi_clearselected()

    # surrounding air
    domain = 220.0
    _circle(femm.mi_addnode, femm.mi_addarc, w / 2.0, h / 2.0, domain, 6.0)
    outside = (w / 2.0, h / 2.0 + domain * 0.6)
    femm.mi_addblocklabel(*outside)
    femm.mi_selectlabel(*outside)
    femm.mi_setblockprop("Air", 1, 0, "", 0, 0, 0)
    femm.mi_clearselected()

    femm.mi_makeABC(7, domain, w / 2.0, h / 2.0, 0)
    femm.mi_zoomnatural()
    femm.mi_saveas(_save(os.path.join(OUTPUT_DIR, "frame_%s.fem" % tag)))
    femm.mi_analyze(1)
    femm.mi_loadsolution()


def _frame_operating_point():
    """(|B|, |H|, mu_r) in the limb opposite the winding."""
    probe = (FRAME_W_MM - FRAME_T_MM / 2.0, FRAME_H_MM / 2.0)
    vals = femm.mo_getpointvalues(*probe)
    # A, B1, B2, Sig, E, H1, H2, Je, Js, Mu1, Mu2, Pe, Ph
    b = math.hypot(_re(vals[1]), _re(vals[2]))
    h = math.hypot(_re(vals[5]), _re(vals[6]))
    mu_r = b / (MU0 * h) if h else float("inf")
    mu_reported = _re(vals[9])
    return b, h, mu_r, mu_reported


def test_bh_curve_saturates():
    """Low drive: high mu_r. High drive: B rolls onto the knee, mu_r collapses."""
    results = {}
    for current, tag in ((0.005, "low"), (30.0, "high")):
        _build_frame(current, tag)
        try:
            results[tag] = _frame_operating_point()
            b, h, mu_r, mu_rep = results[tag]
            _note("    %-4s drive %7.3f A (%6.0f A-turns): B=%7.4f T  H=%10.1f A/m"
                  "  mu_r=%9.1f (FEMM reports %.1f)"
                  % (tag, current, current * FRAME_TURNS, b, h, mu_r, mu_rep))
        finally:
            _teardown()

    b_low, h_low, mu_low, mu_rep_low = results["low"]
    b_high, h_high, mu_high, mu_rep_high = results["high"]

    _note("    mean path = %.3f m, so H_mean at high drive should be ~%.0f A/m"
          % (_frame_mean_path_m(), 30.0 * FRAME_TURNS / _frame_mean_path_m()))

    # FEMM's own reported mu_r must track the one derived from B and H
    for tag, mu_r, mu_rep in (("low", mu_low, mu_rep_low),
                              ("high", mu_high, mu_rep_high)):
        if mu_rep:
            rel = abs(mu_r - mu_rep) / mu_rep
            assert rel < 0.10, (
                "at %s drive mu_r derived from B/(mu0*H) is %.1f but FEMM "
                "reports Mu1=%.1f at the same point (%.0f%% apart)"
                % (tag, mu_r, mu_rep, rel * 100.0))

    assert mu_low > 200.0, (
        "expected a high initial permeability for %s, got mu_r=%.1f -- a BH "
        "curve read as linear (mu_r=1) is exactly what this catches"
        % (FRAME_MATERIAL, mu_low))
    assert mu_high < mu_low / 10.0, (
        "mu_r barely moved between the two drives (%.1f -> %.1f); the material "
        "is not saturating, so the BH curve is not being followed"
        % (mu_low, mu_high))
    assert h_high > h_low
    assert b_high < 2.4, "B = %.3f T is above any physical iron saturation" % b_high
    assert b_high > b_low


# ---------------------------------------------------------------------------
# 6. Time-harmonic: skin effect in a round conductor
#
#   delta = sqrt(2 / (omega * mu * sigma))
#
# One skin depth below the surface the current density magnitude must have
# fallen to 1/e of its surface value. Also asserts AC resistance exceeds DC,
# which is the practical consequence and fails loudly if the harmonic solve
# is quietly running as a magnetostatic one.
# ---------------------------------------------------------------------------

SKIN_R_MM = 5.0
SKIN_FREQ_HZ = 100000.0
SKIN_SIGMA = 58.0e6          # copper, S/m
SKIN_I_A = 1.0
SKIN_LEN_MM = 1000.0         # 1 m of conductor
SKIN_TOL_PCT = 25.0          # a meshed exponential decay near a curved surface


def _skin_depth():
    omega = 2.0 * math.pi * SKIN_FREQ_HZ
    return math.sqrt(2.0 / (omega * MU0 * SKIN_SIGMA))


def _build_skin():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    femm.openfemm(1)
    femm.newdocument(0)
    femm.mi_probdef(SKIN_FREQ_HZ, "millimeters", "planar", 1e-9, SKIN_LEN_MM, 30)

    femm.mi_getmaterial("Air")
    femm.mi_getmaterial("Copper")
    femm.mi_addcircprop("ac", SKIN_I_A, 1)

    delta_mm = _skin_depth() * 1000.0
    _circle(femm.mi_addnode, femm.mi_addarc, 0, 0, SKIN_R_MM, delta_mm / 4.0)
    femm.mi_addblocklabel(0, 0)
    femm.mi_selectlabel(0, 0)
    # mesh finer than the skin depth, or the decay simply cannot be resolved
    femm.mi_setblockprop("Copper", 0, delta_mm / 4.0, "ac", 0, 0, 1)
    femm.mi_clearselected()

    domain = 60.0
    _circle(femm.mi_addnode, femm.mi_addarc, 0, 0, domain, 3.0)
    femm.mi_addblocklabel(domain / 2.0, 0)
    femm.mi_selectlabel(domain / 2.0, 0)
    femm.mi_setblockprop("Air", 1, 0, "", 0, 0, 0)
    femm.mi_clearselected()

    femm.mi_makeABC(7, domain, 0, 0, 0)
    femm.mi_zoomnatural()
    femm.mi_saveas(_save(os.path.join(OUTPUT_DIR, "skin.fem")))
    femm.mi_analyze(1)
    femm.mi_loadsolution()


@pytest.fixture(scope="module")
def skin():
    _build_skin()
    try:
        yield
    finally:
        _teardown()


def _current_density_at(r_mm):
    """|J_total| = |Je + Js|.

    mo_getpointvalues returns the induced (eddy) and source components
    separately -- A, B1, B2, Sig, E, H1, H2, Je, Js, Mu1, Mu2, Pe, Ph --
    and the skin-effect profile lives in their sum, not in Je alone.
    """
    vals = femm.mo_getpointvalues(r_mm, 0.0)
    return abs(complex(vals[7]) + complex(vals[8]))


def test_skin_depth_decay(skin):
    """|J| one skin depth below the surface must be ~1/e of the surface value."""
    delta_mm = _skin_depth() * 1000.0
    _note("    f = %.0f Hz -> skin depth = %.4f mm (conductor radius %.1f mm, "
          "r/delta = %.1f)" % (SKIN_FREQ_HZ, delta_mm, SKIN_R_MM,
                               SKIN_R_MM / delta_mm))

    j_surface = _current_density_at(SKIN_R_MM - delta_mm * 0.05)
    j_one_delta = _current_density_at(SKIN_R_MM - delta_mm)
    j_centre = _current_density_at(0.0)

    ratio_fem = j_one_delta / j_surface
    ratio_exact = 1.0 / math.e
    _note("    |J| surface=%.4g  1 delta in=%.4g  centre=%.4g A/m^2"
          % (j_surface, j_one_delta, j_centre))
    err = _record("skin effect", "|J| ratio at 1 delta", ratio_fem, ratio_exact,
                  "-", SKIN_TOL_PCT)
    assert err < SKIN_TOL_PCT
    assert j_centre < j_surface, (
        "current density at the centre (%.4g) is not below the surface value "
        "(%.4g) -- there is no skin effect at all" % (j_centre, j_surface))


def test_skin_effect_raises_resistance(skin):
    """AC resistance must exceed the DC value at this frequency."""
    femm.mo_selectblock(0, 0)
    losses = _re(femm.mo_blockintegral(4))   # 4 = resistive losses, W
    femm.mo_clearblock()

    # FEMM reports circuit current as an amplitude, so P = 0.5*I_pk^2*R
    r_ac = 2.0 * losses / (SKIN_I_A ** 2)
    area = math.pi * _mm(SKIN_R_MM) ** 2
    r_dc = _mm(SKIN_LEN_MM) / (SKIN_SIGMA * area)
    _note("    R_ac = %.6g ohm, R_dc = %.6g ohm, ratio = %.2f"
          % (r_ac, r_dc, r_ac / r_dc))
    assert r_ac > r_dc, (
        "AC resistance (%.4g) must exceed DC (%.4g) at %.0f Hz -- if it does "
        "not, the harmonic solve is not producing skin effect at all"
        % (r_ac, r_dc, SKIN_FREQ_HZ))


def test_write_report():
    path = _write_report()
    assert os.path.exists(path)
    print("report: " + path)
