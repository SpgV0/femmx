r"""
Where this checkout's built binaries actually are.

Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12 (issue
#24, found by the new fast CI lane on its first run).

Ten test modules hardcoded `<repo>/bin/plain`, which is where
build_femmx.ps1 (and so build_plain.bat / build_cuda.bat) puts things --
it moves everything build.ps1 leaves in `bin\` into `bin\plain\` or
`bin\cuda\` so a CPU and a CUDA build cannot clobber each other. CI does
not use that wrapper: it calls build.ps1 directly, which installs into
`bin\` and stops.

So on CI `bin/plain` did not exist, and every test keyed to it either
failed outright or -- much worse -- skipped, quietly. The first run of
the fast lane reported 16 skips against 2 locally, which is what made it
visible at all. A skip is not a pass, and a suite that silently tests a
fraction of what it thinks it does on the one machine nobody watches
interactively is worse than one that fails.

The resolution order below asks the authoritative question first: which
femmx.exe does COM actually launch? That is the binary every test in this
suite drives, so it is by definition the right directory, and it stays
right if someone registers a different variant.
"""

import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)

CLSID = "{0A35D5BD-DCA9-4C39-9512-1D89A1A37047}"

# Conventional locations, most specific first. bin/ last because it is
# where an un-wrapped build.ps1 leaves things, which is also where a
# wrapper run leaves only static data files behind.
CANDIDATES = (
    os.path.join(REPO_ROOT, "bin", "plain"),
    os.path.join(REPO_ROOT, "bin", "cuda"),
    os.path.join(REPO_ROOT, "bin"),
)


def _registered_bin_dir():
    """The directory of the COM-registered femmx.exe, if it is in this repo.

    Deliberately ignores a registration pointing outside the checkout (an
    installed copy, say): those tests are about this build, and silently
    testing an installation instead would be its own kind of wrong.
    """
    try:
        import winreg
    except ImportError:
        return None
    try:
        with winreg.OpenKey(
                winreg.HKEY_CURRENT_USER,
                r"Software\Classes\CLSID\%s\LocalServer32" % CLSID) as key:
            value = winreg.QueryValueEx(key, "")[0]
    except OSError:
        return None
    path = (value or "").strip().strip('"')
    if not path or not os.path.exists(path):
        return None
    directory = os.path.dirname(os.path.abspath(path))
    root = os.path.abspath(REPO_ROOT)
    if os.path.commonpath([directory, root]) != root:
        return None
    return directory


def bin_dir():
    """Directory holding femmx.exe and the solvers for this checkout.

    Falls back to bin/plain when nothing is built, so error messages name
    the conventional location rather than something arbitrary.
    """
    registered = _registered_bin_dir()
    if registered and os.path.exists(os.path.join(registered, "femmx.exe")):
        return registered
    for candidate in CANDIDATES:
        if os.path.exists(os.path.join(candidate, "femmx.exe")):
            return candidate
    return CANDIDATES[0]


BIN_DIR = bin_dir()
