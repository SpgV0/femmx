"""
Correctness + benchmark test for fkn.exe's optional CUDA-accelerated linear
solve (see fkn/spars_cuda.cu, femm/femmeLua.cpp's mi_setgpuaccel).

Builds one moderately large magnetostatics problem (a current-carrying wire
in a fine-meshed air domain, tens of thousands of nodes -- GPU acceleration
is not expected to help on FEMM's typical small problems; see
gpu_speedup_investigation in the project's Claude memory for the sizing
rationale), solves it once with mi_setgpuaccel(0) (CPU, the default) and
once with mi_setgpuaccel(1) (GPU if available), and checks:

  1. Correctness: both solves agree on the computed flux density at a probe
     point to within a tight tolerance. This holds whether or not this
     build of fkn.exe actually has CUDA support -- if it doesn't,
     mi_setgpuaccel(1) is a no-op and both runs are the same CPU solve, so
     this check passes trivially and is still a meaningful regression
     guard on the GPUAccel plumbing itself (i.e. that turning it on
     doesn't corrupt anything).
  2. Speedup: only asserted if this build appears to actually have CUDA
     support (heuristic: the CUDA runtime DLLs are bundled next to
     fkn.exe, per fkn/CMakeLists.txt's ENABLE_CUDA_SOLVER install step) --
     otherwise this is skipped, since there's nothing to speed up.

Requirements: pip install pyfemm pywin32; a built + COM-registered
femm.exe. For the speedup assertion specifically: fkn.exe built with
-DENABLE_CUDA_SOLVER=ON (see fkn/CMakeLists.txt) and a CUDA-capable GPU.

Usage:
    pytest gpu_solver_test.py -v
    python gpu_solver_test.py
"""

import glob
import math
import os
import time

import pytest

import femm

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
RESULTS_DIR = os.path.join(SCRIPT_DIR, "results", "gpu_solver_test")
RESULTS_PATH = os.path.join(RESULTS_DIR, "gpu_solver.txt")

WIRE_RADIUS_MM = 1.0
DOMAIN_RADIUS_MM = 60.0
AIR_MESH_SIZE_MM = 0.6  # small enough to get tens of thousands of nodes
CURRENT_A = 10.0
PROBE_POINT = (20, 0)
MAX_RELATIVE_DIFFERENCE_PCT = 0.1  # CPU vs GPU results must agree this closely

# Heuristic for "this build actually has CUDA support": the runtime DLLs
# fkn/CMakeLists.txt bundles when built with -DENABLE_CUDA_SOLVER=ON.
BIN_DIR = os.path.join(SCRIPT_DIR, "..", "bin")


def _cuda_build_available():
    return bool(glob.glob(os.path.join(BIN_DIR, "cudart64_*.dll")))


def build_model(model_path, gpu):
    femm.newdocument(0)
    femm.mi_probdef(0, "millimeters", "planar", 1e-8, 1, 30)
    femm.mi_getmaterial("Air")
    femm.mi_getmaterial("Copper")
    femm.mi_addcircprop("icoil", CURRENT_A, 1)

    femm.mi_addnode(WIRE_RADIUS_MM, 0)
    femm.mi_addnode(-WIRE_RADIUS_MM, 0)
    femm.mi_addarc(WIRE_RADIUS_MM, 0, -WIRE_RADIUS_MM, 0, 180, 2.5)
    femm.mi_addarc(-WIRE_RADIUS_MM, 0, WIRE_RADIUS_MM, 0, 180, 2.5)
    femm.mi_addblocklabel(0, 0)
    femm.mi_selectlabel(0, 0)
    femm.mi_setblockprop("Copper", 0, 0.05, "icoil", 0, 0, 1)
    femm.mi_clearselected()

    femm.mi_addnode(DOMAIN_RADIUS_MM, 0)
    femm.mi_addnode(-DOMAIN_RADIUS_MM, 0)
    femm.mi_addarc(DOMAIN_RADIUS_MM, 0, -DOMAIN_RADIUS_MM, 0, 180, 2.5)
    femm.mi_addarc(-DOMAIN_RADIUS_MM, 0, DOMAIN_RADIUS_MM, 0, 180, 2.5)
    femm.mi_addblocklabel(DOMAIN_RADIUS_MM / 2, 0)
    femm.mi_selectlabel(DOMAIN_RADIUS_MM / 2, 0)
    femm.mi_setblockprop("Air", 0, AIR_MESH_SIZE_MM, "", 0, 0, 0)
    femm.mi_clearselected()

    femm.mi_makeABC(7, DOMAIN_RADIUS_MM, 0, 0, 0)

    if gpu:
        femm.callfemm("mi_setgpuaccel(1)")

    femm.mi_saveas(model_path)
    femm.mi_createmesh()


def solve_and_measure(model_path, gpu):
    femm.openfemm()
    try:
        build_model(model_path, gpu)
        t0 = time.perf_counter()
        femm.mi_analyze(1)
        elapsed = time.perf_counter() - t0
        femm.mi_loadsolution()
        bx, by = femm.mo_getb(*PROBE_POINT)
    finally:
        try:
            femm.mo_close()
        except Exception:  # noqa: BLE001
            pass
        femm.closefemm()
    return bx, by, elapsed


@pytest.fixture(scope="module")
def gpu_vs_cpu_result():
    os.makedirs(RESULTS_DIR, exist_ok=True)
    cpu_path = os.path.join(RESULTS_DIR, "gpu_solver_cpu.fem")
    gpu_path = os.path.join(RESULTS_DIR, "gpu_solver_gpu.fem")

    bx_cpu, by_cpu, t_cpu = solve_and_measure(cpu_path, gpu=False)
    bx_gpu, by_gpu, t_gpu = solve_and_measure(gpu_path, gpu=True)

    b_cpu = math.hypot(bx_cpu, by_cpu)
    b_gpu = math.hypot(bx_gpu, by_gpu)
    rel_diff_pct = abs(b_gpu - b_cpu) / b_cpu * 100
    speedup = t_cpu / t_gpu if t_gpu > 0 else float("inf")
    cuda_available = _cuda_build_available()

    lines = [
        "GPU-accelerated solve: correctness + benchmark",
        "================================================",
        f"CUDA-enabled build detected: {cuda_available}",
        f"Probe point: {PROBE_POINT}",
        "",
        f"CPU: |B| = {b_cpu:.6e} T  (time: {t_cpu:.3f}s)",
        f"GPU: |B| = {b_gpu:.6e} T  (time: {t_gpu:.3f}s)",
        f"Relative difference: {rel_diff_pct:.4f}%",
        f"Speedup: {speedup:.2f}x",
    ]
    with open(RESULTS_PATH, "w") as f:
        f.write("\n".join(lines) + "\n")

    return {
        "b_cpu": b_cpu,
        "b_gpu": b_gpu,
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
            "no CUDA runtime DLLs found next to fkn.exe -- this build doesn't "
            "appear to have -DENABLE_CUDA_SOLVER=ON; nothing to benchmark"
        )
    assert gpu_vs_cpu_result["speedup"] > 1.0, (
        f"GPU solve ({gpu_vs_cpu_result['t_gpu']:.3f}s) was not faster than "
        f"CPU ({gpu_vs_cpu_result['t_cpu']:.3f}s) on a "
        f"{AIR_MESH_SIZE_MM}mm-mesh problem. See {RESULTS_PATH}"
    )


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__, "-v"]))
