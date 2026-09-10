"""
Analytic correctness tests for the electrostatics, heat-flow and
current-flow solvers (belasolv, hsolv, csolv).

Until this file existed, test/straight_wire_field_test.py was the only
test in the suite that checked a NUMBER against physics, and it covers
magnetostatics only. The other three solvers were exercised solely by
lua_command_regression_test.py, which is an explicit smoke test: it
asserts that commands run, not that the fields they return are right. A
sign error or a unit-scaling regression in any of those three would have
shipped green (issue #1).

Each case below builds a model from Python via pyfemm, solves it, probes
the solution, and compares against a closed form. The geometries are
chosen so that the closed form is EXACT for the modelled region rather
than an approximation, which is what makes a 2% tolerance meaningful:

  parallel plate      Neumann side walls make the ideal, fringing-free
                      uniform field the exact solution of the modelled
                      region, not just its large-plate limit.
  coaxial (axi)       dV/dz = 0 already satisfies the natural boundary
                      condition on the flat ends, so the log profile is
                      exact with no end effects.
  slab / shell        same argument, for temperature.

Requirements: a built, COM-registered femmx.exe; pip install pyfemm pywin32.

Usage:
    pytest test/analytic_fields_test.py -v
"""

import math
import os

import pytest

import femm

EPS0 = 8.8541878128e-12      # F/m
MAX_REL_ERROR_PCT = 2.0      # matches straight_wire_field_test.py

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "results", "analytic_fields")

_REPORT = []


def _mm(x):
    """millimetres -> metres."""
    return x / 1000.0


def _re(value, tol=1e-6):
    """Real part of a FEMM result.

    belasolv and csolv both support frequency-domain analysis, so their
    post-processors hand back complex phasors even for a DC problem. At DC
    the imaginary part must be numerically zero; assert that rather than
    quietly discarding it, since a non-zero one would mean the solve was
    not actually static.
    """
    if isinstance(value, complex):
        mag = abs(value)
        if mag and abs(value.imag) / mag > tol:
            raise AssertionError(
                "expected a DC (real) result, got %r with a significant "
                "imaginary part" % (value,))
        return value.real
    return value


def _mag(a, b):
    """Magnitude of a vector whose components may be complex phasors."""
    return math.hypot(_re(a), _re(b))


def _teardown(closer):
    """Close a post-processor and the FEMM session, tolerantly.

    Every model fixture here is module-scoped, so they all tear down at the
    end of the module. The first closefemm() clears pyfemm's module-level
    HandleToFEMM, and every later teardown would then die with
    "NameError: name 'HandleToFEMM' is not defined" -- a teardown artefact
    that says nothing about the physics under test.
    """
    for fn in (closer, femm.closefemm):
        try:
            fn()
        except Exception:  # noqa: BLE001 -- teardown must not mask results
            pass

def _record(name, quantity, fem_value, analytic, unit):
    fem_value = _re(fem_value)
    err = abs(fem_value - analytic) / abs(analytic) * 100.0 if analytic else float("inf")
    line = ("%-28s %-22s FEM=%12.6g  analytic=%12.6g %-6s  err=%6.2f %%"
            % (name, quantity, fem_value, analytic, unit, err))
    print(line)
    _REPORT.append(line)
    return err


def _write_report():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    path = os.path.join(OUTPUT_DIR, "analytic_fields.txt")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("Analytic correctness checks for belasolv / hsolv / csolv" + os.linesep)
        fh.write("=" * 70 + os.linesep)
        for line in _REPORT:
            fh.write(line + os.linesep)
    return path


def _rect(add_node, add_seg, x0, y0, x1, y1):
    """Draw an axis-aligned rectangle; returns its four edge midpoints."""
    for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        add_node(x, y)
    add_seg(x0, y0, x1, y0)
    add_seg(x1, y0, x1, y1)
    add_seg(x1, y1, x0, y1)
    add_seg(x0, y1, x0, y0)
    return dict(bottom=((x0 + x1) / 2.0, y0),
                right=(x1, (y0 + y1) / 2.0),
                top=((x0 + x1) / 2.0, y1),
                left=(x0, (y0 + y1) / 2.0))


# ---------------------------------------------------------------------------
# Electrostatics: parallel-plate capacitor
#
#   E = V0 / d                    (uniform, exact with Neumann side walls)
#   C = eps0 * eps_r * A / d      A = plate width * problem depth
# ---------------------------------------------------------------------------

PP_WIDTH_MM = 20.0
PP_GAP_MM = 5.0
PP_DEPTH_MM = 1.0
PP_VOLTS = 100.0
PP_EPSR = 1.0


def _build_parallel_plate():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    femm.openfemm(1)
    femm.newdocument(1)  # electrostatics
    femm.ei_probdef("millimeters", "planar", 1e-9, PP_DEPTH_MM, 30)

    femm.ei_addmaterial("dielectric", PP_EPSR, PP_EPSR, 0)
    femm.ei_addboundprop("Vlow", 0, 0, 0, 0, 0)
    femm.ei_addboundprop("Vhigh", PP_VOLTS, 0, 0, 0, 0)

    edges = _rect(femm.ei_addnode, femm.ei_addsegment,
                  0, 0, PP_WIDTH_MM, PP_GAP_MM)

    # Only the plates carry a boundary condition. The side walls are left
    # natural (dV/dn = 0), which is what makes the uniform field exact here.
    femm.ei_selectsegment(*edges["bottom"])
    femm.ei_setsegmentprop("Vlow", 0, 1, 0, 0, "")
    femm.ei_clearselected()
    femm.ei_selectsegment(*edges["top"])
    femm.ei_setsegmentprop("Vhigh", 0, 1, 0, 0, "")
    femm.ei_clearselected()

    femm.ei_addblocklabel(PP_WIDTH_MM / 2.0, PP_GAP_MM / 2.0)
    femm.ei_selectlabel(PP_WIDTH_MM / 2.0, PP_GAP_MM / 2.0)
    femm.ei_setblockprop("dielectric", 1, 0, 0)
    femm.ei_clearselected()

    femm.ei_zoomnatural()
    femm.ei_saveas(os.path.join(OUTPUT_DIR, "parallel_plate.fee").replace(chr(92), "/"))
    femm.ei_analyze(1)
    femm.ei_loadsolution()


@pytest.fixture(scope="module")
def parallel_plate():
    _build_parallel_plate()
    try:
        yield
    finally:
        _teardown(femm.eo_close)


def test_parallel_plate_field(parallel_plate):
    """E between the plates must equal V0/d."""
    vals = femm.eo_getpointvalues(PP_WIDTH_MM / 2.0, PP_GAP_MM / 2.0)
    ex, ey = vals[3], vals[4]
    e_fem = _mag(ex, ey)
    e_exact = PP_VOLTS / _mm(PP_GAP_MM)
    err = _record("parallel plate", "|E| mid-gap", e_fem, e_exact, "V/m")
    assert err < MAX_REL_ERROR_PCT


def test_parallel_plate_potential_is_linear(parallel_plate):
    """V must rise linearly across the gap."""
    worst = 0.0
    for frac in (0.25, 0.5, 0.75):
        y = PP_GAP_MM * frac
        v_fem = femm.eo_getpointvalues(PP_WIDTH_MM / 2.0, y)[0]
        v_exact = PP_VOLTS * frac
        worst = max(worst, _record("parallel plate", "V at y=%.2f mm" % y,
                                   v_fem, v_exact, "V"))
    assert worst < MAX_REL_ERROR_PCT


def test_parallel_plate_capacitance(parallel_plate):
    """C from stored energy must equal eps0*eps_r*A/d."""
    femm.eo_selectblock(PP_WIDTH_MM / 2.0, PP_GAP_MM / 2.0)
    # eo_blockintegral returns [real, imaginary], not a scalar.
    energy = _re(femm.eo_blockintegral(0)[0])   # 0 = stored energy, J
    femm.eo_clearblock()

    c_fem = 2.0 * energy / (PP_VOLTS ** 2)
    area = _mm(PP_WIDTH_MM) * _mm(PP_DEPTH_MM)
    c_exact = EPS0 * PP_EPSR * area / _mm(PP_GAP_MM)
    err = _record("parallel plate", "C from energy", c_fem, c_exact, "F")
    assert err < MAX_REL_ERROR_PCT


# ---------------------------------------------------------------------------
# Electrostatics, axisymmetric: coaxial cylinder
#
#   V(r) = V0 * ln(b/r) / ln(b/a)
#
# Exact for the modelled region: dV/dz = 0 satisfies the natural boundary
# condition on the flat ends, so there are no end effects to approximate.
# ---------------------------------------------------------------------------

COAX_A_MM = 2.0
COAX_B_MM = 10.0
COAX_H_MM = 6.0
COAX_VOLTS = 50.0


def _build_coax():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    femm.openfemm(1)
    femm.newdocument(1)
    femm.ei_probdef("millimeters", "axi", 1e-9, 1, 30)

    femm.ei_addmaterial("dielectric", 1, 1, 0)
    femm.ei_addboundprop("Vinner", COAX_VOLTS, 0, 0, 0, 0)
    femm.ei_addboundprop("Vouter", 0, 0, 0, 0, 0)

    edges = _rect(femm.ei_addnode, femm.ei_addsegment,
                  COAX_A_MM, 0, COAX_B_MM, COAX_H_MM)

    femm.ei_selectsegment(*edges["left"])     # r = a
    femm.ei_setsegmentprop("Vinner", 0, 1, 0, 0, "")
    femm.ei_clearselected()
    femm.ei_selectsegment(*edges["right"])    # r = b
    femm.ei_setsegmentprop("Vouter", 0, 1, 0, 0, "")
    femm.ei_clearselected()

    mid = ((COAX_A_MM + COAX_B_MM) / 2.0, COAX_H_MM / 2.0)
    femm.ei_addblocklabel(*mid)
    femm.ei_selectlabel(*mid)
    femm.ei_setblockprop("dielectric", 1, 0, 0)
    femm.ei_clearselected()

    femm.ei_zoomnatural()
    femm.ei_saveas(os.path.join(OUTPUT_DIR, "coax.fee").replace(chr(92), "/"))
    femm.ei_analyze(1)
    femm.ei_loadsolution()


@pytest.fixture(scope="module")
def coax():
    _build_coax()
    try:
        yield
    finally:
        _teardown(femm.eo_close)


def test_coax_logarithmic_potential(coax):
    """V(r) must follow the coaxial log profile."""
    worst = 0.0
    denom = math.log(COAX_B_MM / COAX_A_MM)
    for r in (3.0, 5.0, 8.0):
        v_fem = femm.eo_getpointvalues(r, COAX_H_MM / 2.0)[0]
        v_exact = COAX_VOLTS * math.log(COAX_B_MM / r) / denom
        worst = max(worst, _record("coax (axi)", "V at r=%.1f mm" % r,
                                   v_fem, v_exact, "V"))
    assert worst < MAX_REL_ERROR_PCT


def test_coax_radial_field(coax):
    """E_r = V0 / (r * ln(b/a))."""
    r = 5.0
    vals = femm.eo_getpointvalues(r, COAX_H_MM / 2.0)
    e_fem = _mag(vals[3], vals[4])
    e_exact = COAX_VOLTS / (_mm(r) * math.log(COAX_B_MM / COAX_A_MM))
    err = _record("coax (axi)", "|E| at r=%.1f mm" % r, e_fem, e_exact, "V/m")
    assert err < MAX_REL_ERROR_PCT


# ---------------------------------------------------------------------------
# Heat flow: 1-D slab with fixed hot and cold faces
#
#   T(x) linear;   q = k * dT / L      (W/m^2)
# ---------------------------------------------------------------------------

SLAB_L_MM = 10.0
SLAB_H_MM = 4.0
SLAB_K = 5.0            # W/(m.K)
SLAB_T_HOT = 400.0
SLAB_T_COLD = 300.0


def _build_slab():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    femm.openfemm(1)
    femm.newdocument(2)  # heat flow
    femm.hi_probdef("millimeters", "planar", 1e-9, 1, 30)

    # A plain linear conductivity. The shipped library materials carry a
    # temperature-dependent table, which would make this a nonlinear solve
    # and take the closed form away (see issue #6).
    femm.hi_addmaterial("slab", SLAB_K, SLAB_K, 0, 0)
    femm.hi_addboundprop("Thot", 0, SLAB_T_HOT, 0, 0, 0, 0)
    femm.hi_addboundprop("Tcold", 0, SLAB_T_COLD, 0, 0, 0, 0)

    edges = _rect(femm.hi_addnode, femm.hi_addsegment,
                  0, 0, SLAB_L_MM, SLAB_H_MM)

    femm.hi_selectsegment(*edges["left"])
    femm.hi_setsegmentprop("Thot", 0, 1, 0, 0, "")
    femm.hi_clearselected()
    femm.hi_selectsegment(*edges["right"])
    femm.hi_setsegmentprop("Tcold", 0, 1, 0, 0, "")
    femm.hi_clearselected()

    mid = (SLAB_L_MM / 2.0, SLAB_H_MM / 2.0)
    femm.hi_addblocklabel(*mid)
    femm.hi_selectlabel(*mid)
    femm.hi_setblockprop("slab", 1, 0, 0)
    femm.hi_clearselected()

    femm.hi_zoomnatural()
    femm.hi_saveas(os.path.join(OUTPUT_DIR, "slab.feh").replace(chr(92), "/"))
    femm.hi_analyze(1)
    femm.hi_loadsolution()


@pytest.fixture(scope="module")
def slab():
    _build_slab()
    try:
        yield
    finally:
        _teardown(femm.ho_close)


def test_slab_linear_temperature(slab):
    """T must fall linearly from the hot face to the cold face."""
    worst = 0.0
    for frac in (0.25, 0.5, 0.75):
        x = SLAB_L_MM * frac
        t_fem = femm.ho_getpointvalues(x, SLAB_H_MM / 2.0)[0]
        t_exact = SLAB_T_HOT + (SLAB_T_COLD - SLAB_T_HOT) * frac
        worst = max(worst, _record("1-D slab", "T at x=%.2f mm" % x,
                                   t_fem, t_exact, "K"))
    assert worst < MAX_REL_ERROR_PCT


def test_slab_heat_flux(slab):
    """Flux density must equal k*dT/L."""
    vals = femm.ho_getpointvalues(SLAB_L_MM / 2.0, SLAB_H_MM / 2.0)
    f_fem = math.hypot(vals[1], vals[2])
    f_exact = SLAB_K * (SLAB_T_HOT - SLAB_T_COLD) / _mm(SLAB_L_MM)
    err = _record("1-D slab", "|F| mid-slab", f_fem, f_exact, "W/m^2")
    assert err < MAX_REL_ERROR_PCT


# ---------------------------------------------------------------------------
# Heat flow, axisymmetric: cylindrical shell
#
#   T(r) = T_b + (T_a - T_b) * ln(b/r) / ln(b/a)
# ---------------------------------------------------------------------------

SHELL_A_MM = 2.0
SHELL_B_MM = 10.0
SHELL_H_MM = 6.0
SHELL_K = 3.0
SHELL_T_INNER = 500.0
SHELL_T_OUTER = 300.0


def _build_shell():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    femm.openfemm(1)
    femm.newdocument(2)
    femm.hi_probdef("millimeters", "axi", 1e-9, 1, 30)

    femm.hi_addmaterial("shell", SHELL_K, SHELL_K, 0, 0)
    femm.hi_addboundprop("Tin", 0, SHELL_T_INNER, 0, 0, 0, 0)
    femm.hi_addboundprop("Tout", 0, SHELL_T_OUTER, 0, 0, 0, 0)

    edges = _rect(femm.hi_addnode, femm.hi_addsegment,
                  SHELL_A_MM, 0, SHELL_B_MM, SHELL_H_MM)

    femm.hi_selectsegment(*edges["left"])
    femm.hi_setsegmentprop("Tin", 0, 1, 0, 0, "")
    femm.hi_clearselected()
    femm.hi_selectsegment(*edges["right"])
    femm.hi_setsegmentprop("Tout", 0, 1, 0, 0, "")
    femm.hi_clearselected()

    mid = ((SHELL_A_MM + SHELL_B_MM) / 2.0, SHELL_H_MM / 2.0)
    femm.hi_addblocklabel(*mid)
    femm.hi_selectlabel(*mid)
    femm.hi_setblockprop("shell", 1, 0, 0)
    femm.hi_clearselected()

    femm.hi_zoomnatural()
    femm.hi_saveas(os.path.join(OUTPUT_DIR, "shell.feh").replace(chr(92), "/"))
    femm.hi_analyze(1)
    femm.hi_loadsolution()


@pytest.fixture(scope="module")
def shell():
    _build_shell()
    try:
        yield
    finally:
        _teardown(femm.ho_close)


def test_shell_logarithmic_temperature(shell):
    """T(r) must follow the cylindrical-shell log profile."""
    worst = 0.0
    denom = math.log(SHELL_B_MM / SHELL_A_MM)
    for r in (3.0, 5.0, 8.0):
        t_fem = femm.ho_getpointvalues(r, SHELL_H_MM / 2.0)[0]
        t_exact = (SHELL_T_OUTER
                   + (SHELL_T_INNER - SHELL_T_OUTER) * math.log(SHELL_B_MM / r) / denom)
        worst = max(worst, _record("cyl shell (axi)", "T at r=%.1f mm" % r,
                                   t_fem, t_exact, "K"))
    assert worst < MAX_REL_ERROR_PCT


def test_shell_radial_flux(shell):
    """Radial flux density must equal k*(Ta-Tb)/(r*ln(b/a))."""
    r = 5.0
    vals = femm.ho_getpointvalues(r, SHELL_H_MM / 2.0)
    f_fem = math.hypot(vals[1], vals[2])
    f_exact = (SHELL_K * (SHELL_T_INNER - SHELL_T_OUTER)
               / (_mm(r) * math.log(SHELL_B_MM / SHELL_A_MM)))
    err = _record("cyl shell (axi)", "|F| at r=%.1f mm" % r, f_fem, f_exact, "W/m^2")
    assert err < MAX_REL_ERROR_PCT


# ---------------------------------------------------------------------------
# Heat flow: convection boundary
#
# A wall held at T_hot on one face, losing heat to ambient through a film
# coefficient h on the other. In steady state the same flux crosses both,
# so with L/k the conduction resistance and 1/h the film resistance:
#
#   q = (T_hot - T_inf) / (L/k + 1/h)
#   T_surface = T_inf + q/h
# ---------------------------------------------------------------------------

CONV_L_MM = 8.0
CONV_H_MM = 4.0
CONV_K = 2.0
CONV_T_HOT = 450.0
CONV_T_INF = 300.0
CONV_H_COEF = 250.0      # W/(m^2.K)


def _build_convection():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    femm.openfemm(1)
    femm.newdocument(2)
    femm.hi_probdef("millimeters", "planar", 1e-9, 1, 30)

    femm.hi_addmaterial("wall", CONV_K, CONV_K, 0, 0)
    femm.hi_addboundprop("Thot", 0, CONV_T_HOT, 0, 0, 0, 0)
    # BdryType 2 = convection: (name, type, Tset, qs, Tinf, h, beta)
    femm.hi_addboundprop("film", 2, 0, 0, CONV_T_INF, CONV_H_COEF, 0)

    edges = _rect(femm.hi_addnode, femm.hi_addsegment,
                  0, 0, CONV_L_MM, CONV_H_MM)

    femm.hi_selectsegment(*edges["left"])
    femm.hi_setsegmentprop("Thot", 0, 1, 0, 0, "")
    femm.hi_clearselected()
    femm.hi_selectsegment(*edges["right"])
    femm.hi_setsegmentprop("film", 0, 1, 0, 0, "")
    femm.hi_clearselected()

    mid = (CONV_L_MM / 2.0, CONV_H_MM / 2.0)
    femm.hi_addblocklabel(*mid)
    femm.hi_selectlabel(*mid)
    femm.hi_setblockprop("wall", 1, 0, 0)
    femm.hi_clearselected()

    femm.hi_zoomnatural()
    femm.hi_saveas(os.path.join(OUTPUT_DIR, "convection.feh").replace(chr(92), "/"))
    femm.hi_analyze(1)
    femm.hi_loadsolution()


@pytest.fixture(scope="module")
def convection():
    _build_convection()
    try:
        yield
    finally:
        _teardown(femm.ho_close)


def test_convection_flux_and_surface_temperature(convection):
    """Series conduction + film resistance sets both flux and surface T."""
    q_exact = (CONV_T_HOT - CONV_T_INF) / (_mm(CONV_L_MM) / CONV_K + 1.0 / CONV_H_COEF)
    vals = femm.ho_getpointvalues(CONV_L_MM / 2.0, CONV_H_MM / 2.0)
    q_fem = math.hypot(vals[1], vals[2])
    err_q = _record("convection wall", "|F| mid-wall", q_fem, q_exact, "W/m^2")

    t_surf_exact = CONV_T_INF + q_exact / CONV_H_COEF
    # probe just inside the convecting face
    t_surf_fem = femm.ho_getpointvalues(CONV_L_MM * 0.999, CONV_H_MM / 2.0)[0]
    err_t = _record("convection wall", "T at outer face", t_surf_fem,
                    t_surf_exact, "K")
    assert err_q < MAX_REL_ERROR_PCT
    assert err_t < MAX_REL_ERROR_PCT


# ---------------------------------------------------------------------------
# Current flow: rectangular bar between two fixed potentials
#
#   R = rho * L / A = L / (sigma * W * depth)
#   I = V / R
#   J = sigma * V / L        (uniform)
#
# Real power from co_blockintegral(0) gives an independent route to R,
# since P = V^2 / R for a DC resistor.
# ---------------------------------------------------------------------------

BAR_L_MM = 12.0
BAR_W_MM = 4.0
BAR_DEPTH_MM = 2.0
BAR_SIGMA = 1.0e6        # S/m
BAR_VOLTS = 5.0


def _build_bar():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    femm.openfemm(1)
    femm.newdocument(3)  # current flow
    # NOTE the extra argument: only current flow takes a frequency.
    #   ei_probdef(units, type,            precision, depth, minangle)
    #   hi_probdef(units, type,            precision, depth, minangle)
    #   ci_probdef(units, type, frequency, precision, depth, minangle)
    # Passing the 5-argument form shifts every value along: precision
    # becomes the depth and minangle becomes the depth, which is how a
    # 2mm-deep bar was silently solved 30mm deep.
    femm.ci_probdef("millimeters", "planar", 0, 1e-9, BAR_DEPTH_MM, 30)

    # ci_addmaterial(name, ox, oy, ex, ey, ltx, lty); conductivity in MS/m
    femm.ci_addmaterial("bar", BAR_SIGMA, BAR_SIGMA, 1, 1, 0, 0)
    femm.ci_addboundprop("Vlow", 0, 0, 0, 0, 0)
    femm.ci_addboundprop("Vhigh", BAR_VOLTS, 0, 0, 0, 0)

    edges = _rect(femm.ci_addnode, femm.ci_addsegment,
                  0, 0, BAR_L_MM, BAR_W_MM)

    femm.ci_selectsegment(*edges["left"])
    femm.ci_setsegmentprop("Vhigh", 0, 1, 0, 0, "")
    femm.ci_clearselected()
    femm.ci_selectsegment(*edges["right"])
    femm.ci_setsegmentprop("Vlow", 0, 1, 0, 0, "")
    femm.ci_clearselected()

    mid = (BAR_L_MM / 2.0, BAR_W_MM / 2.0)
    femm.ci_addblocklabel(*mid)
    femm.ci_selectlabel(*mid)
    femm.ci_setblockprop("bar", 1, 0, 0)
    femm.ci_clearselected()

    femm.ci_zoomnatural()
    femm.ci_saveas(os.path.join(OUTPUT_DIR, "bar.fec").replace(chr(92), "/"))
    femm.ci_analyze(1)
    femm.ci_loadsolution()


@pytest.fixture(scope="module")
def bar():
    _build_bar()
    try:
        yield
    finally:
        _teardown(femm.co_close)


def _bar_resistance():
    area = _mm(BAR_W_MM) * _mm(BAR_DEPTH_MM)
    return _mm(BAR_L_MM) / (BAR_SIGMA * area)


def test_bar_linear_potential(bar):
    """V must fall linearly along the bar."""
    worst = 0.0
    for frac in (0.25, 0.5, 0.75):
        x = BAR_L_MM * frac
        v_fem = femm.co_getpointvalues(x, BAR_W_MM / 2.0)[0]
        v_exact = BAR_VOLTS * (1.0 - frac)
        worst = max(worst, _record("current bar", "V at x=%.2f mm" % x,
                                   v_fem, v_exact, "V"))
    assert worst < MAX_REL_ERROR_PCT


def test_bar_current_density(bar):
    """J = sigma * E = sigma * V / L, uniform along the bar."""
    vals = femm.co_getpointvalues(BAR_L_MM / 2.0, BAR_W_MM / 2.0)
    j_fem = _mag(vals[1], vals[2])
    j_exact = BAR_SIGMA * BAR_VOLTS / _mm(BAR_L_MM)
    err = _record("current bar", "|J| mid-bar", j_fem, j_exact, "A/m^2")
    assert err < MAX_REL_ERROR_PCT


def test_bar_resistance_from_power(bar):
    """P = V^2/R, so dissipated power gives R independently of the geometry."""
    femm.co_selectblock(BAR_L_MM / 2.0, BAR_W_MM / 2.0)
    power = _re(femm.co_blockintegral(0))   # 0 = real power, W
    femm.co_clearblock()

    r_exact = _bar_resistance()
    r_fem = (BAR_VOLTS ** 2) / power
    err_r = _record("current bar", "R from power", r_fem, r_exact, "ohm")

    i_exact = BAR_VOLTS / r_exact
    i_fem = power / BAR_VOLTS
    err_i = _record("current bar", "I from power", i_fem, i_exact, "A")
    assert err_r < MAX_REL_ERROR_PCT
    assert err_i < MAX_REL_ERROR_PCT


def test_write_report():
    """Emit the collected comparison table under test/results/."""
    path = _write_report()
    assert os.path.exists(path)
    print("report: " + path)
