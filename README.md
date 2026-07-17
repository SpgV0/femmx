# FEMMX

FEMMX is a fork of [FEMM 4.2](https://www.femm.info/) (Finite Element Method
Magnetics), a Windows application for solving 2D planar and axisymmetric
problems in:

* Magnetics (magnetostatic and time-harmonic/eddy-current)
* Electrostatics
* Heat flow
* Current flow (DC conduction)

Problems are built and meshed in a native MFC editor (`femmx.exe`), solved by
one of four solver executables (`fkn.exe` for magnetics/current-flow,
`csolv.exe` for electrostatics, `hsolv.exe` for heat flow, `belasolv.exe` for
a Kelvin-transformation belt-solver variant), and results are inspected in a
built-in post-processor. Problems can also be driven entirely by script via
Lua (built in), Octave/Matlab (`octavefemm`), Mathematica (`mathfemm`),
Scilab (`scifemm`), or Python ([`pyfemm`](https://www.femm.info/wiki/pyFEMM),
via the `femm.ActiveFEMM` COM automation server).

This fork is hosted at https://github.com/SpgV0/femmx as a fork of
https://github.com/cenit/FEMM. It adds an optional CUDA-accelerated linear
solve, a CPU/GPU load monitor, dark-theme editor/results views, an installer
that mirrors the original `C:\femm42` layout with automatic COM
self-registration, and a pytest-based regression suite -- see
[CHANGELOG.md](CHANGELOG.md) for the full, dated history of every change
since the [22Oct2023](https://github.com/cenit/FEMM) upstream baseline, and
[NOTICE.md](NOTICE.md) for the license-required per-file modification
record.

FEMMX is distributed under the terms of the Aladdin Free Public License --
see [license.txt](license.txt).

## Requirements

Building and running FEMMX requires **Windows** (the project is MFC/Win32
native code; there is no cross-platform build). You'll need:

* [Git](https://git-scm.com/)
* [CMake](https://cmake.org/) 3.10 or later
* Visual Studio 2019 or 2022, with the **"Desktop development with C++"**
  workload, which must include the **MFC and ATL** optional component
  (`find_package(MFC REQUIRED)` in several `CMakeLists.txt` files depends on
  it)

Optional, depending on what you want to build:

* [NSIS](https://nsis.sourceforge.io/) (`makensis` on `PATH`) -- to build the
  installer (`FEMMX_v<version>_installer.exe`). If not found, the
  executables still build fine; only installer packaging is skipped.
* [NVIDIA CUDA Toolkit](https://developer.nvidia.com/cuda-toolkit), **12.x**
  (13.x dropped compute capability `sm_60`, one of this project's default
  target architectures) -- to build `fkn.exe`'s optional GPU-accelerated
  linear solver. Also needs an NVIDIA GPU to actually use at runtime (it
  falls back to the CPU solver automatically if none is found). CUDA's
  MSVC-support window tends to lag behind the newest installed Visual
  Studio, so a VS2022 toolset is also needed alongside a newer VS if one is
  installed (auto-detected by `build_cuda.bat`; see below).
* A LaTeX distribution (e.g. [MiKTeX](https://miktex.org/)), with `latex` on
  `PATH` -- to build the PDF manual (`manual/manual.tex.in`). Skipped
  automatically if not found.
* [Python](https://www.python.org/) 3.9+ with the packages in
  [`test/requirements.txt`](test/requirements.txt) -- to run the regression
  suite under `test/` (see [test/README.md](test/README.md)).

## Building

The simplest way to build is the zero-argument wrapper scripts, which
auto-detect all of the above and print diagnostics for anything missing:

```
build_plain.bat
```
builds `femmx.exe` and the other executables without GPU support, into
`bin\plain\`.

```
build_cuda.bat
```
additionally builds `fkn.exe`'s CUDA-accelerated linear solver, into
`bin\cuda\`. Auto-detects the CUDA Toolkit root (preferring a 12.x install)
and an `nvcc`-compatible VS2022 toolset; pass a CUDA Toolkit path as the
first argument to override, or set the `FEMM_CUDA_CCBIN` environment
variable to force a specific MSVC toolset.

Either way, once the build finishes you'll have:

* `bin\<plain|cuda>\femmx.exe` -- the main application
* `bin\<plain|cuda>\FEMMX_v<version>_installer.exe` -- the NSIS installer
  (if NSIS was found)

Both scripts are thin wrappers (`build_femmx.ps1`) around the underlying
`build.ps1`, which can also be invoked directly for more control, e.g.:

```
.\build.ps1 -DisableInteractive -DisableLaTeX -ForceTriangle32bit `
  -AdditionalBuildSetup "-DENABLE_CUDA_SOLVER=ON -DFEMM_CUDA_ROOT=`"C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.6`""
```

`build.ps1` runs `cmake --build . --target install`, which places
executables directly in `bin\` (not the `plain\`/`cuda\` split -- that split
is done by `build_femmx.ps1` afterwards so a plain and a CUDA build don't
clobber each other).

### Useful CMake options

Pass these via `build.ps1`'s `-AdditionalBuildSetup "..."` parameter, or
directly to `cmake` if configuring by hand:

| Option | Default | Effect |
|---|---|---|
| `ENABLE_CUDA_SOLVER` | `OFF` | Build `fkn.exe`'s CUDA-accelerated solver. Requires `FEMM_CUDA_ROOT`. |
| `FEMM_CUDA_ROOT` | (none) | Path to the CUDA Toolkit, e.g. `C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6` |
| `FEMM_CUDA_CCBIN` | (none) | Optional `nvcc -ccbin` override: an alternate MSVC toolset `bin` dir |
| `FEMM_CUDA_ARCHS` | `60;75;86;89` | CUDA compute capabilities to build for |
| `BUILD_MANUAL` | `OFF` | Build the PDF manual (needs LaTeX) |
| `SKIP_<target>` | `OFF` | Skip building an individual component, e.g. `SKIP_scifemm` |

## Installing

Run the installer produced by the build (`bin\plain\FEMMX_v<version>_
installer.exe` or `bin\cuda\FEMMX_v<version>_installer.exe`). It installs to
a fixed `C:\FEMMX`, laid out like the original FEMM 4.2 installer:

```
C:\FEMMX\
  bin\        executables and data libraries
  mathfemm\   Mathematica interface
  mfiles\     Octave/Matlab interface (octavefemm)
  scifemm\    Scilab interface
```

and self-registers the `femm.ActiveFEMM` COM automation class (used by
`pyfemm`, Octave, Matlab, Mathematica, and Scilab), so scripting works
immediately after install with no separate manual registration step. A CUDA
build's installer also bundles the required CUDA runtime DLLs.

## Testing

The regression suite under `test/` builds and solves FEMM models through the
`pyfemm` COM interface and checks results, timings, and Lua-command
coverage. See [test/README.md](test/README.md) for details; in short:

```
pip install -r test/requirements.txt
pytest test/
```

## More information

* [CHANGELOG.md](CHANGELOG.md) -- dated history of every change, this fork
  and upstream
* [NOTICE.md](NOTICE.md) -- license-required modification record
* [test/README.md](test/README.md) -- regression suite details
* [manual/](manual/) -- LaTeX source for the user manual
* Upstream: https://github.com/cenit/FEMM, https://www.femm.info/
