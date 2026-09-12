"""
Shared pytest fixtures for the test regression suite.

All tests here drive a real, built femmx.exe over its COM automation
interface (femm.ActiveFEMM). If femmx.exe hasn't been built, or the COM
class isn't registered, or pywin32/pyfemm aren't installed, the whole
suite is skipped with a clear reason rather than erroring -- this lets
`pytest test/` run harmlessly on a machine that hasn't set up the
FEMM toolchain, while still failing loudly in CI (where setup is expected
to have happened).
"""

import os
import winreg

import pytest

try:
    import femm
except ImportError:
    femm = None


def pytest_addoption(parser):
    """--update-goldens, for render_golden_test.py.

    Regenerating reference images is deliberate, never automatic: an
    intentional visual change should be an explicit commit containing the
    new PNGs, reviewable as a picture. Without the flag a missing or
    differing reference is a failure, which is the point.
    """
    parser.addoption(
        "--update-goldens",
        action="store_true",
        default=False,
        help="rewrite render_golden_test.py's reference PNGs from this run",
    )
    parser.addoption(
        "--update-references",
        action="store_true",
        default=False,
        help="rewrite solver_regression_test.py's stored solver results",
    )

def _femm_available():
    if femm is None:
        return False, "pyfemm is not installed (pip install pyfemm pywin32)"
    try:
        import win32com.client  # noqa: F401
    except ImportError:
        return False, "pywin32 is not installed (pip install pywin32)"
    # Check the COM class is registered without actually launching femmx.exe
    # (avoids spawning a process just to probe availability at collection time).
    for hive in (winreg.HKEY_CURRENT_USER, winreg.HKEY_CLASSES_ROOT):
        try:
            with winreg.OpenKey(hive, r"Software\Classes\femm.ActiveFEMM"
                                 if hive == winreg.HKEY_CURRENT_USER else "femm.ActiveFEMM"):
                return True, ""
        except FileNotFoundError:
            continue
    return False, (
        "femm.ActiveFEMM is not registered as a COM class. Build femmx.exe and "
        "register it (see scripts/register_femm_com.ps1)."
    )


def pytest_configure(config):
    """Register the markers this suite uses.

    "slow" lets CI run a fast lane with -m "not slow" while a full run
    still exercises the larger stress models.

    "static" marks a test that reads the repository and nothing else --
    no femmx.exe, no COM, no build. Added for issue #20: the bridge
    wrapper checks are exactly that, and the whole point of them is that
    they run on any checkout, so they must not be swept up by the
    session-wide COM skip below.
    """
    config.addinivalue_line(
        "markers", "slow: a long-running case, excluded from the fast lane")
    config.addinivalue_line(
        "markers",
        "static: reads the repository only -- needs no femmx.exe or COM")
    config.addinivalue_line(
        "markers", "smoke: the fast lane -- broad, cheap, run on every push")
    config.addinivalue_line(
        "markers", "gpu: exercises the optional CUDA solver path")
    config.addinivalue_line(
        "markers", "physics: asserts field values, not just that calls ran")


# Markers applied per module rather than per test, by
# pytest_collection_modifyitems below. One table beats a decorator on
# every one of ~200 tests: it can be read in full, and a module that
# joins the suite without being listed here is visible as an omission
# rather than silently untagged. Per-test markers still work and compose
# with these (see @pytest.mark.slow on individual cases).
#
# "smoke" is chosen on measured time, not by feel. The modules below
# totalled about 12 seconds of the suite's 130 on this machine, and
# between them they touch the COM layer, the file formats, the mesher,
# the material library, DXF, the solvers and this fork's own scripting
# additions -- which is the point of a smoke set: broad and cheap, not a
# random fast subset.
MODULE_MARKERS = {
    "smoke": (
        "bridge_wrappers_test",
        "com_automation_test",
        "dxf_roundtrip_test",
        "enforce_pslg_correctness_test",
        "file_format_roundtrip_test",
        "fork_scripting_test",
        "material_library_test",
        "mesh_generation_test",
        "solver_regression_test",
        "straight_wire_field_test",
    ),
    "gpu": (
        "ac_gpu_solver_test",
        "currentflow_gpu_solver_test",
        "electrostatic_gpu_solver_test",
        "gpu_solver_test",
        "thermal_gpu_solver_test",
    ),
    "physics": (
        "analytic_fields_test",
        "boundary_conditions_test",
        "magnetics_analytic_test",
        "postprocess_parity_test",
        "solver_regression_test",
        "straight_wire_field_test",
    ),
    # Whole modules that are slow end to end, as opposed to individual
    # slow cases inside an otherwise quick module.
    "slow": (
        "copy_redraw_benchmark_test",
        "installer_smoke_test",
        "lua_command_regression_test",
        "parallel_instances_test",
        "render_golden_test",
    ),
}

# Modules deliberately in no group: corrupt_input_test (fast but its
# whole job is feeding malformed files to a real solver, which is not
# something to run on every push).
_UNGROUPED_OK = {"conftest", "corrupt_input_test"}


def pytest_collection_modifyitems(config, items):
    by_module = {}
    for marker, modules in MODULE_MARKERS.items():
        for module in modules:
            by_module.setdefault(module, []).append(marker)

    seen = set()
    for item in items:
        stem = os.path.splitext(os.path.basename(str(item.fspath)))[0]
        seen.add(stem)
        for marker in by_module.get(stem, ()):
            item.add_marker(getattr(pytest.mark, marker))

    untagged = sorted(s for s in seen
                      if s not in by_module and s not in _UNGROUPED_OK)
    if untagged:
        # A warning, not a failure: a new module should not break the run,
        # but it should not silently miss every -m selection either.
        config.issue_config_time_warning(
            pytest.PytestConfigWarning(
                "test modules carry no marker group, so `-m smoke` and the "
                "other selections will skip them entirely -- add them to "
                "MODULE_MARKERS in conftest.py: %s" % ", ".join(untagged)),
            stacklevel=1)

_AVAILABLE, _REASON = _femm_available()


# Function-scoped, not session-scoped: a session fixture's request.node is
# the Session, which carries no per-test markers, so the "static" check
# below would never see one. The availability probe itself still runs once
# -- it is the module-level _AVAILABLE above, not this fixture.
@pytest.fixture(autouse=True)
def _require_femm(request):
    if _AVAILABLE:
        return
    # A static test asserts things about the source tree, so "COM is not
    # registered" is not a reason to skip it -- it is exactly the
    # environment those tests are meant to be useful in.
    if request.node.get_closest_marker("static") is not None:
        return
    pytest.skip(f"femmx.exe COM automation not available: {_REASON}")


@pytest.fixture(autouse=True)
def _results_dir_exists():
    results_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "results")
    os.makedirs(results_dir, exist_ok=True)
