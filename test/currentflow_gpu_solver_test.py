"""
Correctness + benchmark test for csolv.exe's optional CUDA-accelerated
linear solve (see csolv/spars_cuda.cu, femm/CDRAWLUA.CPP's
ci_setgpuaccel), mirroring test/ac_gpu_solver_test.py's magnetics AC test
for the current-flow solver.

Current flow's linear system is always complex-symmetric (conduction +
displacement current, CBigComplexLinProb::PBCGSolveMod) -- there is no
real-valued formulation to fall back to, unlike magnetics/heat/
electrostatics. A single homogeneous material's complex admittivity
(sigma + j*w*epsilon) factors straight out of the governing equation for
a problem with only real Dirichlet boundary conditions, collapsing back
to a purely real field regardless of frequency -- not a meaningful
exercise of the complex solver's arithmetic (see
gpu_speedup_investigation in the project's Claude memory: the AC/harmonic
magnetics test hit the same issue and needed a genuine conductor, not
just a zero-conductivity excitation coil, to get real jwsigma terms).
This test uses TWO concentric ring materials with deliberately different
conductivity *and* permittivity ratios at a high test frequency (not
physically realistic for a "current flow" problem, but sufficient to
make sigma+j*w*epsilon genuinely non-proportional between the two
materials) so the solved voltage field comes out genuinely complex, not
just real-with-a-zero-imaginary-part.

Builds this problem (fine-meshed enough to get tens of thousands of
nodes), solves it once with ci_setgpuaccel(0) (CPU, the default) and once
with ci_setgpuaccel(1) (GPU if available), and checks:

  1. Correctness: both solves agree on the computed complex voltage at a
     probe point to within a tight tolerance (compared by magnitude,
     since co_getpointvalues returns a complex phasor for AC-style
     current-flow solutions -- same reasoning as mo_getb() in the
     magnetics AC test). This holds whether or not this build of
     csolv.exe actually has CUDA support -- if it doesn't,
     ci_setgpuaccel(1) is a no-op and both runs are the same CPU solve,
     so this check passes trivially and is still a meaningful regression
     guard on the GPUAccel plumbing itself.
  2. Speedup: only asserted if this build appears to actually have CUDA
     support (heuristic: the CUDA runtime DLLs are bundled next to
     csolv.exe, per csolv/CMakeLists.txt's ENABLE_CUDA_SOLVER install
     step) -- otherwise this is skipped, since there's nothing to
     benchmark.

Requirements: pip install pyfemm pywin32; a built + COM-registered
femmx.exe. For the speedup assertion specifically: csolv.exe built with
-DENABLE_CUDA_SOLVER=ON (see csolv/CMakeLists.txt) and a CUDA-capable GPU.

Usage:
    pytest currentflow_gpu_solver_test.py -v
    python currentflow_gpu_solver_test.py
"""

import glob
import os
import time

import pytest

import femm

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
RESULTS_DIR = os.path.join(SCRIPT_DIR, "results", "currentflow_gpu_solver_test")
RESULTS_PATH = os.path.join(RESULTS_DIR, "currentflow_gpu_solver.txt")

INNER_RADIUS_MM = 1.0
MID_RADIUS_MM = 20.0
OUTER_RADIUS_MM = 60.0
MESH_SIZE_MM = 0.6  # small enough to get tens of thousands of nodes
V_HOT = 100.0
V_COLD = 0.0
FREQUENCY_HZ = 1.0e9  # deliberately high -- see module docstring
PROBE_POINT = (10, 0)
MAX_RELATIVE_DIFFERENCE_PCT = 0.1  # CPU vs GPU results must agree this closely

# Heuristic for "this build actually has CUDA support": the runtime DLLs
# csolv/CMakeLists.txt bundles when built with -DENABLE_CUDA_SOLVER=ON.
BIN_DIR = os.path.join(SCRIPT_DIR, "..", "bin")


def _cuda_build_available():
    return bool(glob.glob(os.path.join(BIN_DIR, "cudart64_*.dll")))


def build_model(model_path, gpu):
    femm.newdocument(3)
    femm.ci_probdef("millimeters", "planar", FREQUENCY_HZ, 1e-8, 1)
    femm.ci_addmaterial("Layer1", 1, 1, 2, 2, 0, 0)
    femm.ci_addmaterial("Layer2", 0.05, 0.05, 10, 10, 0, 0)
    femm.ci_addboundprop("Hot", V_HOT, 0, 0, 0, 0)
    femm.ci_addboundprop("Cold", V_COLD, 0, 0, 0, 0)

    femm.ci_addnode(INNER_RADIUS_MM, 0)
    femm.ci_addnode(-INNER_RADIUS_MM, 0)
    femm.ci_addarc(INNER_RADIUS_MM, 0, -INNER_RADIUS_MM, 0, 180, 2.5)
    femm.ci_addarc(-INNER_RADIUS_MM, 0, INNER_RADIUS_MM, 0, 180, 2.5)
    femm.ci_selectarcsegment(0, INNER_RADIUS_MM)
    femm.ci_setarcsegmentprop(2.5, "Hot", 0, 0, "")
    femm.ci_clearselected()
    femm.ci_selectarcsegment(0, -INNER_RADIUS_MM)
    femm.ci_setarcsegmentprop(2.5, "Hot", 0, 0, "")
    femm.ci_clearselected()

    # Material interface at MID_RADIUS_MM -- deliberately left without a
    # boundary property assignment, so the solver treats it as a normal
    # internal mesh edge (continuity of V and normal current density
    # enforced automatically by the FEM assembly), not a Dirichlet
    # boundary. Same technique gpu_solver_test.py uses for its wire/air
    # material interface.
    femm.ci_addnode(MID_RADIUS_MM, 0)
    femm.ci_addnode(-MID_RADIUS_MM, 0)
    femm.ci_addarc(MID_RADIUS_MM, 0, -MID_RADIUS_MM, 0, 180, 2.5)
    femm.ci_addarc(-MID_RADIUS_MM, 0, MID_RADIUS_MM, 0, 180, 2.5)

    femm.ci_addnode(OUTER_RADIUS_MM, 0)
    femm.ci_addnode(-OUTER_RADIUS_MM, 0)
    femm.ci_addarc(OUTER_RADIUS_MM, 0, -OUTER_RADIUS_MM, 0, 180, 2.5)
    femm.ci_addarc(-OUTER_RADIUS_MM, 0, OUTER_RADIUS_MM, 0, 180, 2.5)
    femm.ci_selectarcsegment(0, OUTER_RADIUS_MM)
    femm.ci_setarcsegmentprop(2.5, "Cold", 0, 0, "")
    femm.ci_clearselected()
    femm.ci_selectarcsegment(0, -OUTER_RADIUS_MM)
    femm.ci_setarcsegmentprop(2.5, "Cold", 0, 0, "")
    femm.ci_clearselected()

    # The disk inside the inner boundary (r < INNER_RADIUS_MM) is not part
    # of the annulus being modeled -- mark it as a hole ("<No Mesh>") so
    # the mesher excludes it, matching the equivalent fix in
    # thermal_gpu_solver_test.py.
    femm.ci_addblocklabel(0, 0)
    femm.ci_selectlabel(0, 0)
    femm.ci_setblockprop("<No Mesh>", 0, 1, 0)
    femm.ci_clearselected()

    femm.ci_addblocklabel((INNER_RADIUS_MM + MID_RADIUS_MM) / 2, 0)
    femm.ci_selectlabel((INNER_RADIUS_MM + MID_RADIUS_MM) / 2, 0)
    femm.ci_setblockprop("Layer1", 0, MESH_SIZE_MM, 0)
    femm.ci_clearselected()

    femm.ci_addblocklabel((MID_RADIUS_MM + OUTER_RADIUS_MM) / 2, 0)
    femm.ci_selectlabel((MID_RADIUS_MM + OUTER_RADIUS_MM) / 2, 0)
    femm.ci_setblockprop("Layer2", 0, MESH_SIZE_MM, 0)
    femm.ci_clearselected()

    if gpu:
        femm.callfemm("ci_setgpuaccel(1)")

    femm.ci_saveas(model_path)
    femm.ci_createmesh()


def solve_and_measure(model_path, gpu):
    femm.openfemm()
    try:
        build_model(model_path, gpu)
        t0 = time.perf_counter()
        femm.ci_analyze(1)
        elapsed = time.perf_counter() - t0
        femm.ci_loadsolution()
        values = femm.co_getpointvalues(*PROBE_POINT)
        voltage = values[0]
    finally:
        try:
            femm.co_close()
        except Exception:  # noqa: BLE001
            pass
        femm.closefemm()
    return voltage, elapsed


@pytest.fixture(scope="module")
def gpu_vs_cpu_result():
    os.makedirs(RESULTS_DIR, exist_ok=True)
    cpu_path = os.path.join(RESULTS_DIR, "currentflow_gpu_solver_cpu.fec")
    gpu_path = os.path.join(RESULTS_DIR, "currentflow_gpu_solver_gpu.fec")

    v_cpu_val, t_cpu = solve_and_measure(cpu_path, gpu=False)
    v_gpu_val, t_gpu = solve_and_measure(gpu_path, gpu=True)

    v_cpu_mag = abs(v_cpu_val)
    v_gpu_mag = abs(v_gpu_val)
    rel_diff_pct = abs(v_gpu_mag - v_cpu_mag) / v_cpu_mag * 100
    speedup = t_cpu / t_gpu if t_gpu > 0 else float("inf")
    cuda_available = _cuda_build_available()

    lines = [
        "GPU-accelerated current-flow solve: correctness + benchmark",
        "=============================================================",
        f"CUDA-enabled build detected: {cuda_available}",
        f"Probe point: {PROBE_POINT}",
        f"Frequency: {FREQUENCY_HZ:.3e} Hz",
        "",
        f"CPU: V = {v_cpu_val}  |V| = {v_cpu_mag:.6e}  (time: {t_cpu:.3f}s)",
        f"GPU: V = {v_gpu_val}  |V| = {v_gpu_mag:.6e}  (time: {t_gpu:.3f}s)",
        f"Relative difference: {rel_diff_pct:.4f}%",
        f"Speedup: {speedup:.2f}x",
    ]
    with open(RESULTS_PATH, "w") as f:
        f.write("\n".join(lines) + "\n")

    return {
        "v_cpu_val": v_cpu_val,
        "v_gpu_val": v_gpu_val,
        "rel_diff_pct": rel_diff_pct,
        "t_cpu": t_cpu,
        "t_gpu": t_gpu,
        "speedup": speedup,
        "cuda_available": cuda_available,
    }


def test_gpu_result_matches_cpu(gpu_vs_cpu_result):
    assert gpu_vs_cpu_result["rel_diff_pct"] < MAX_RELATIVE_DIFFERENCE_PCT, (
        f"GPU-path result differs from CPU-path result by "
        f"{gpu_vs_cpu_result['rel_diff_pct']:.4f}% (allowed: "
        f"{MAX_RELATIVE_DIFFERENCE_PCT}%). See {RESULTS_PATH}"
    )


def test_gpu_is_faster_when_available(gpu_vs_cpu_result):
    if not gpu_vs_cpu_result["cuda_available"]:
        pytest.skip(
            "no CUDA runtime DLLs found next to csolv.exe -- this build "
            "doesn't appear to have -DENABLE_CUDA_SOLVER=ON; nothing to "
            "benchmark"
        )
    assert gpu_vs_cpu_result["speedup"] > 1.0, (
        f"GPU solve ({gpu_vs_cpu_result['t_gpu']:.3f}s) was not faster than "
        f"CPU ({gpu_vs_cpu_result['t_cpu']:.3f}s) on a "
        f"{MESH_SIZE_MM}mm-mesh problem. See {RESULTS_PATH}"
    )


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__, "-v"]))
