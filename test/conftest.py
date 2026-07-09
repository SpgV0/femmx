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


_AVAILABLE, _REASON = _femm_available()


@pytest.fixture(scope="session", autouse=True)
def _require_femm():
    if not _AVAILABLE:
        pytest.skip(f"femmx.exe COM automation not available: {_REASON}")


@pytest.fixture(autouse=True)
def _results_dir_exists():
    results_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "results")
    os.makedirs(results_dir, exist_ok=True)
