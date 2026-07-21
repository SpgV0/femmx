"""
Correctness + benchmark test for hsolv.exe's optional CUDA-accelerated
linear solve (see hsolv/spars_cuda.cu, femm/HDRAWLUA.CPP's
hi_setgpuaccel), mirroring test/gpu_solver_test.py's magnetics test for
the heat-flow solver.

Builds one moderately large steady-state heat-conduction problem (an
annulus with a fixed hot inner boundary and fixed cold outer boundary,
fine-meshed enough to get tens of thousands of nodes -- GPU acceleration
is not expected to help on FEMM's typical small problems; see
gpu_speedup_investigation in the project's Claude memory for the sizing
rationale that motivated this for the magnetics case), solves it once
with hi_setgpuaccel(0) (CPU, the default) and once with
hi_setgpuaccel(1) (GPU if available), and checks:

  1. Correctness: both solves agree on the computed temperature at a
     probe point to within a tight tolerance. This holds whether or not
     this build of hsolv.exe actually has CUDA support -- if it doesn't,
     hi_setgpuaccel(1) is a no-op and both runs are the same CPU solve,
     so this check passes trivially and is still a meaningful regression
     guard on the GPUAccel plumbing itself (i.e. that turning it on
     doesn't corrupt anything).
  2. Speedup: only asserted if this build appears to actually have CUDA
     support (heuristic: the CUDA runtime DLLs are bundled next to
     hsolv.exe, per hsolv/CMakeLists.txt's ENABLE_CUDA_SOLVER install
     step) -- otherwise this is skipped, since there's nothing to speed
     up.

Requirements: pip install pyfemm pywin32; a built + COM-registered
femmx.exe. For the speedup assertion specifically: hsolv.exe built with
-DENABLE_CUDA_SOLVER=ON (see hsolv/CMakeLists.txt) and a CUDA-capable
GPU.

Usage:
    pytest thermal_gpu_solver_test.py -v
    python thermal_gpu_solver_test.py
"""

import glob
import os
import time

import pytest

import femm

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
RESULTS_DIR = os.path.join(SCRIPT_DIR, "results", "thermal_gpu_solver_test")
RESULTS_PATH = os.path.join(RESULTS_DIR, "thermal_gpu_solver.txt")

INNER_RADIUS_MM = 1.0
OUTER_RADIUS_MM = 60.0
MESH_SIZE_MM = 0.6  # small enough to get tens of thousands of nodes
T_HOT = 100.0
T_COLD = 0.0
CONDUCTIVITY = 1.0
PROBE_POINT = (20, 0)
MAX_RELATIVE_DIFFERENCE_PCT = 0.1  # CPU vs GPU results must agree this closely

# Heuristic for "this build actually has CUDA support": the runtime DLLs
# hsolv/CMakeLists.txt bundles when built with -DENABLE_CUDA_SOLVER=ON.
BIN_DIR = os.path.join(SCRIPT_DIR, "..", "bin")


def _cuda_build_available():
    return bool(glob.glob(os.path.join(BIN_DIR, "cudart64_*.dll")))


def build_model(model_path, gpu):
    femm.newdocument(2)
    femm.hi_probdef("millimeters", "planar", 1e-8, 1, 30)
    femm.hi_addmaterial("Conductor", CONDUCTIVITY, CONDUCTIVITY, 0)
    femm.hi_addboundprop("Hot", 0, T_HOT)
    femm.hi_addboundprop("Cold", 0, T_COLD)

    femm.hi_addnode(INNER_RADIUS_MM, 0)
    femm.hi_addnode(-INNER_RADIUS_MM, 0)
    femm.hi_addarc(INNER_RADIUS_MM, 0, -INNER_RADIUS_MM, 0, 180, 2.5)
    femm.hi_addarc(-INNER_RADIUS_MM, 0, INNER_RADIUS_MM, 0, 180, 2.5)
    femm.hi_selectarcsegment(0, INNER_RADIUS_MM)
    femm.hi_setarcsegmentprop(2.5, "Hot", 0, 0, "")
    femm.hi_clearselected()
    femm.hi_selectarcsegment(0, -INNER_RADIUS_MM)
    femm.hi_setarcsegmentprop(2.5, "Hot", 0, 0, "")
    femm.hi_clearselected()

    # The disk inside the inner boundary (r < INNER_RADIUS_MM) is not part
    # of the annulus being modeled -- mark it as a hole ("<No Mesh>") so
    # the mesher excludes it, matching how every other enclosed region
    # needs an explicit block label (real material or hole).
    femm.hi_addblocklabel(0, 0)
    femm.hi_selectlabel(0, 0)
    femm.hi_setblockprop("<No Mesh>", 0, 1, 0)
    femm.hi_clearselected()

    femm.hi_addnode(OUTER_RADIUS_MM, 0)
    femm.hi_addnode(-OUTER_RADIUS_MM, 0)
    femm.hi_addarc(OUTER_RADIUS_MM, 0, -OUTER_RADIUS_MM, 0, 180, 2.5)
    femm.hi_addarc(-OUTER_RADIUS_MM, 0, OUTER_RADIUS_MM, 0, 180, 2.5)
    femm.hi_selectarcsegment(0, OUTER_RADIUS_MM)
    femm.hi_setarcsegmentprop(2.5, "Cold", 0, 0, "")
    femm.hi_clearselected()
    femm.hi_selectarcsegment(0, -OUTER_RADIUS_MM)
    femm.hi_setarcsegmentprop(2.5, "Cold", 0, 0, "")
    femm.hi_clearselected()

    femm.hi_addblocklabel(OUTER_RADIUS_MM / 2, 0)
    femm.hi_selectlabel(OUTER_RADIUS_MM / 2, 0)
    femm.hi_setblockprop("Conductor", 0, MESH_SIZE_MM, 0)
    femm.hi_clearselected()

    if gpu:
        femm.callfemm("hi_setgpuaccel(1)")

    femm.hi_saveas(model_path)
    femm.hi_createmesh()


def solve_and_measure(model_path, gpu):
    femm.openfemm()
    try:
        build_model(model_path, gpu)
        t0 = time.perf_counter()
        femm.hi_analyze(1)
        elapsed = time.perf_counter() - t0
        femm.hi_loadsolution()
        values = femm.ho_getpointvalues(*PROBE_POINT)
        temperature = values[0]
    finally:
        try:
            femm.ho_close()
        except Exception:  # noqa: BLE001
            pass
        femm.closefemm()
    return temperature, elapsed


@pytest.fixture(scope="module")
def gpu_vs_cpu_result():
    os.makedirs(RESULTS_DIR, exist_ok=True)
    cpu_path = os.path.join(RESULTS_DIR, "thermal_gpu_solver_cpu.feh")
    gpu_path = os.path.join(RESULTS_DIR, "thermal_gpu_solver_gpu.feh")

    t_cpu_val, t_cpu = solve_and_measure(cpu_path, gpu=False)
    t_gpu_val, t_gpu = solve_and_measure(gpu_path, gpu=True)

    rel_diff_pct = abs(t_gpu_val - t_cpu_val) / abs(t_cpu_val) * 100
    speedup = t_cpu / t_gpu if t_gpu > 0 else float("inf")
    cuda_available = _cuda_build_available()

    lines = [
        "GPU-accelerated thermal solve: correctness + benchmark",
        "========================================================",
        f"CUDA-enabled build detected: {cuda_available}",
        f"Probe point: {PROBE_POINT}",
        "",
        f"CPU: T = {t_cpu_val:.6e}  (time: {t_cpu:.3f}s)",
        f"GPU: T = {t_gpu_val:.6e}  (time: {t_gpu:.3f}s)",
        f"Relative difference: {rel_diff_pct:.4f}%",
        f"Speedup: {speedup:.2f}x",
    ]
    with open(RESULTS_PATH, "w") as f:
        f.write("\n".join(lines) + "\n")

    return {
        "t_cpu_val": t_cpu_val,
        "t_gpu_val": t_gpu_val,
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
            "no CUDA runtime DLLs found next to hsolv.exe -- this build "
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
