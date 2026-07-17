17Jul2026 (v1.1.1)

* Fixed .ans load taking minutes on large solved models (e.g. a
  transformer model with an 8.7M-node/element mesh: 384.5s -> 10.4s to
  open, 37x). Three compounding issues:
  - build.ps1 referenced a $DoNotUseNinja switch that was never actually
    declared as a parameter, so the branch that set "--config Release"
    for cmake's build step could never run. No -G was ever passed
    either, so cmake always fell back to its own default (a multi-config
    Visual Studio generator), and building that with no --config
    defaults to Debug. Every build produced by this script -- dev
    builds, the packaged installer, and CI -- has therefore always been
    an unoptimized Debug build of every target, despite the
    build_win_release* folder names. $selectConfig is now set
    unconditionally.
  - CFemmviewDoc::OnOpenDocument (femm/FemmviewDoc.cpp, and the
    belaview/cview/hview equivalents) parsed each mesh node/element line
    with sscanf, far slower than manual parsing for a file with millions
    of such lines. Replaced with strtod/strtol walking a pointer along
    the line.
  - The "multiply defined block labels" correctness check called
    InTriangle() once per block label to find which mesh element
    contains that label's point. InTriangle() searches outward from
    wherever the previous call left off -- fast for spatially coherent
    queries, but block labels here are scattered all over the model, so
    each call degraded towards an O(elements) scan, making the whole
    check O(labels x elements); this one loop alone accounted for 93% of
    the load time above. Elements are now binned into a uniform grid
    once, and each query searches outward cell-by-cell from its own
    position instead -- same "will eventually find it" guarantee, O(1)
    average instead of O(elements) per query.

17Jul2026 (v1.1.0)

* Batched the pre-/post-processor mesh-line drawing (femm/FemmeView.cpp,
  femm/FemmviewView.cpp, femm/beladrawView.cpp, femm/belaviewView.cpp,
  femm/cdrawView.cpp, femm/cviewView.cpp, femm/hdrawView.cpp,
  femm/hviewView.cpp): one MoveTo()+LineTo() GDI call pair per mesh edge
  (plus a GetClientRect() per edge for off-screen clamping) made large-mesh
  visualization slow -- hundreds of thousands of individual GDI/Win32 calls
  per repaint. Edges are now accumulated into a capped buffer and flushed
  via PolyPolyline(), mirroring the density-plot PolyPolygon batching
  already in place.
* Fixed pan/zoom input feeling delayed while a large mesh redraws: the
  pre-processor geometry/mesh views had no message-pumping during OnDraw at
  all, so a slow redraw fully blocked input until it finished; the
  post-processor result views had a Pump() helper meant to keep the UI
  responsive, but it dispatched the pending pan/zoom keystroke mid-draw
  while the current redraw kept rendering with now-stale cached
  coordinates, and also risked a reentrant OnDraw call by dispatching
  WM_PAINT itself. Pump() (added to the 4 pre-processor views, fixed in the
  4 post-processor views) now detects pending navigation input before
  dispatching anything and reports it back as a cancellation signal, no
  longer dispatches WM_PAINT, and every OnDraw drawing stage checks that
  signal and bails out immediately so a stale frame ends fast and the newer
  input is processed right away.
* Added build_all.bat, a zero-argument wrapper that runs build_plain.bat
  and build_cuda.bat back to back for a local CPU-only + CUDA build in one
  step.
* Versioned the application: the main window title (IDR_MAINFRAME) now
  reads "FEMMX v1.1.0", VERSIONINFO's FILEVERSION/PRODUCTVERSION (femm.rc)
  are updated to match (previously stale leftovers from the pre-fork femm
  4.2 baseline), and the installer (script.nsi) now builds as
  FEMMX_v1.1.0_installer.exe and records DisplayVersion in the uninstall
  registry key. Also rebranded IDD_ABOUTBOX, which still read "About femm"
  / "femm 4.2" after the FEMMX rename, to "About FEMMX" / "FEMMX v1.1.0".

10Jul2026 (v1.0.0)

* Rehosted this repository as a fork, FEMMX, at
  https://github.com/SpgV0/femmx, cloned from
  https://github.com/cenit/FEMM commit 7d9e8ed. See NOTICE.md for the
  license-required modification record.
* Added test/, with Python scripts that build and solve FEMM
  models via the pyfemm COM interface: straight_wire_field.py (a
  current-carrying-wire magnetostatics problem validated against the
  closed-form Ampere's-law solution), copy_redraw_benchmark.py,
  enforce_pslg_correctness_test.py, and lua_command_regression_test.py
  (see next items). Each script writes its generated files under its own
  test/results/<script_name>/ subfolder.
* Added mi_setredraw(flag) Lua/scripting command
  (femm/femmeLua.cpp, femm/FemmeDoc.h) to suspend the magnetics editor's
  canvas redraw during batch edits, e.g. repeated mi_copytranslate/
  mi_copyrotate calls. Fixed DrawPSLG() (femm/FemmeView.cpp), which
  didn't honor the existing NoDraw suppression flag unlike OnDraw(), and
  wired the Edit > Copy/Move dialogs to use the same suspend/resume
  pattern.
* Fixed CFemmeDoc::EnforcePSLG() (femm/MOVECOPY.CPP): called once per
  Copy operation, it used to rebuild the entire node/segment/arc/block
  list from scratch via intersection-checking Add* calls, making it
  O(n^2) in total feature count -- the dominant Copy cost on large
  drawings. RotateCopy/TranslateCopy only ever append new geometry to
  the end of each list, so a new EnforcePSLG(tol, nodeStart, lineStart,
  arcStart, blockStart) overload now only re-validates the newly added
  tail (still checked against the full existing drawing, so correctness
  is unchanged -- see test/enforce_pslg_correctness_test.py).
  Combined with the mi_setredraw fix above, measured 9.4x speedup over
  30 repeated copy actions against a 1,600-block-label model; see
  test/results/copy_redraw_benchmark/copy_benchmark.txt.
* Added test/lua_command_regression_test.py, a regression sweep
  over FEMM's ~450-function Lua command surface (magnetics, electro-
  statics, heat flow, and current flow, both pre- and post-processor
  prefixes), tracking every command call as PASS/FAIL/SKIP so a change
  to the shared editing code used by every problem type's editor shows
  up here. Surfaced a few pre-existing, unrelated issues along the way
  (a savebitmap bug in all four pre-processor editors, a pyfemm-exposed
  ci_refreshview that isn't actually a registered Lua command, a bug in
  pyfemm's own AWG/IEC helpers) -- see test/README.md and
  test/results/lua_command_regression/lua_command_regression.txt
  for details. Magnetics (the editor this fork modifies) passes cleanly
  apart from the savebitmap issue above.
* Experimental, isolated in its own commit for easy revert: added a
  "Dark Theme" toggle to the magnetics editor's View menu
  (ID_VIEW_DARKTHEME; femm/femm.rc, femm/resource.h, femm/FemmeView.h,
  femm/FemmeView.cpp). Swaps the canvas colors (background, grid, node,
  line, block, mesh, selection, name) between the light defaults and a
  dark palette, and best-effort switches the main window's title bar via
  DwmSetWindowAttribute/DWMWA_USE_IMMERSIVE_DARK_MODE (Windows 10
  1809+/11; a no-op elsewhere). Scope: only the magnetics editor's
  canvas and title bar are re-themed -- menus, toolbars, and dialogs
  still use the OS's native (light) common-control rendering, since
  re-theming those would require owner-drawing every control.
* Converted the regression scripts under test/ (renamed from
  test_models, then unittests) into pytest-based unit tests with
  skip-logic for machines missing prerequisites, and added a GitHub
  Actions CI workflow (.github/workflows/ccpp.yml) that builds FEMM
  and the NSIS installer and runs the full suite on windows-latest.
* Added an optional CUDA-accelerated linear solve for fkn.exe's
  magnetostatic/DC solver (fkn/spars_cuda.cu/.h): a CSR-based,
  GPU-resident Jacobi-preconditioned conjugate gradient implementation
  using cuSPARSE/cuBLAS, mirroring CBigLinProb::PCGSolve step for step.
  Off by default; opt in per-problem via the "Use GPU Acceleration"
  checkbox in the Problem Definition dialog (femm/probdlg.cpp) or the
  mi_setgpuaccel(flag) Lua command (femm/femmeLua.cpp), persisted in the
  .fem file's [GPUAccel] field. Falls back to the CPU solver at run
  time if no usable GPU is found, or shows a dialog with setup
  instructions if fkn.exe was built without CUDA support at all.
  Building it requires -DENABLE_CUDA_SOLVER=ON and the CUDA Toolkit
  (see fkn/CMakeLists.txt); a normal build.ps1 build is unaffected.
  Validated correct (0.0000% relative difference from the CPU solver)
  and faster (1.3x on a ~70k-node problem, 3.5x on a ~700k-node
  problem) on real hardware; see test/gpu_solver_test.py.
* Added a CPU/GPU load monitor window (fkn/LoadMonitorDlg.h/.cpp),
  shown alongside the existing solver progress dialog whenever fkn.exe
  is solving. Plots a rolling 60-second strip chart of CPU utilization
  (via GetSystemTimes) and, when an NVIDIA GPU and driver are present,
  GPU utilization (via NVML, loaded dynamically so this doesn't require
  the CUDA Toolkit to build). Includes a "Save as PNG..." button
  (GDI+). Builds unconditionally, independent of ENABLE_CUDA_SOLVER.
  The window now also stays open after the solve finishes (for
  interactive/non-scripted runs only -- scripted mi_analyze() calls,
  which femmx.exe always waits on synchronously, are detected and exit
  promptly as before) so the final chart and "Save as PNG" remain
  usable until the user closes it.
* Extended the CUDA-accelerated linear solve to fkn.exe's harmonic
  (AC/eddy-current) solver (fkn/spars_cuda.cu's CudaPBCGSolve,
  fkn/cspars.cpp's PBCGSolveGPU): the same Jacobi-preconditioner swap as
  the DC solver above, but for CBigComplexLinProb's complex-symmetric
  system (cuSPARSE/cuBLAS complex-valued CSR SpMV and dot products).
  Uses the same GPUAccel opt-in (checkbox/mi_setgpuaccel/.fem field) as
  the DC case -- one setting now covers both solvers. Profiling showed
  the linear solve is ~85% of total solve time (assembly and meshing are
  comparatively negligible), so this was the highest-value remaining
  optimization target; see test/ac_gpu_solver_test.py. Validated correct
  (0.0000% relative difference from the CPU solver) and faster (2.2-2.4x
  on a ~40k-node eddy-current problem) on real hardware.
* Documented mi_setgpuaccel and mi_setredraw -- the two Lua commands
  added since the femm-4.2-22Oct2023 pre-fork baseline -- in the
  manual's Lua Scripting chapter (manual/magnlua.tex, Problem Commands
  and Editing Commands sections respectively).
* Rebranded the fork from femm_plus to FEMMX: the main GUI/COM-server
  executable is now femmx.exe (femm/CMakeLists.txt), with the window
  title, VERSIONINFO strings, installer (script.nsi), CI workflow, and
  the Mathematica/Octave interface scripts (mathfemm/mathfemm.m,
  octavefemm/mfiles/openfemm.m) updated to match. The femm.ActiveFEMM
  COM ProgID, the solver executables (fkn.exe/csolv.exe/hsolv.exe/
  belasolv.exe), and the femm/ source directory and file names are all
  unchanged by this rename -- see scripts/register_femm_com.ps1 for how
  the ProgID's LocalServer32 registry entry now points at femmx.exe.
  Also removed the now-superseded spgryparis/femm_mods hosting
  reference from this file and NOTICE.md, keeping only the original
  https://github.com/cenit/FEMM upstream reference and the current
  https://github.com/SpgV0/femmx hosting.
* Added build_plain.bat and build_cuda.bat, zero-argument wrappers
  around build.ps1 (via the new build_femmx.ps1) for a local CPU-only
  or CUDA-enabled dev build. build_cuda.bat auto-detects the CUDA
  Toolkit root (preferring 12.x over 13.x, which dropped compute
  capability sm_60) and an nvcc-compatible MSVC toolset (-ccbin),
  falling back to VS2022's if a newer Visual Studio is installed. Each
  outputs to bin/plain/ or bin/cuda/ so the two builds don't clobber
  each other. Fixed two bugs found while building this: build.ps1's
  folder cleanup never matched build_win_release64_notriangle (the
  directory -ForceTriangle32bit actually creates), so a cached
  ENABLE_CUDA_SOLVER=ON could silently leak into the next CPU-only
  build; and -ccbin alone doesn't override the INCLUDE environment
  variable VsDevCmd.bat sets for the newest installed Visual Studio, so
  nvcc kept resolving MSVC STL headers new enough to require CUDA 13.2+
  even with an older -ccbin (fkn/CMakeLists.txt now adds an explicit
  -I for -ccbin's own toolset headers, which take precedence).
* Restructured the installer (script.nsi) to match the original FEMM
  4.2 installer's C:\femm42 layout: executables now install to
  $INSTDIR\bin (previously flat in $INSTDIR), and the Mathematica/
  Octave/Scilab interfaces are packaged too, at $INSTDIR\mathfemm,
  \mfiles, and \scifemm -- matching what mathfemm.m and octavefemm's
  openfemm.m already hardcoded. Defaults to a fixed C:\FEMMX install
  directory (was $APPDATA\FEMMX) to match. The installer now also
  bundles the CUDA runtime DLLs for a CUDA build (previously left out,
  so an installed CUDA build silently fell back to CPU), and
  self-registers the femm.ActiveFEMM COM class on install/uninstall
  (see scripts/register_femm_com.ps1's docstring for why femmx.exe's
  own COM self-registration doesn't currently work under this CMake
  build) -- so pyfemm/Octave/Mathematica/Scilab automation works
  immediately after a normal install, no separate manual step needed.
  Fixed a CMake ordering bug found while building this: the installer-
  build step was running before the install(TARGETS...) steps that
  copy executables into bin/, so it packaged stale leftovers rather
  than the current build -- moved into its own subdirectory
  (installer/CMakeLists.txt), added last, since sibling subdirectories
  preserve add_subdirectory() call order but a directory's own
  install() rules do not run after its subdirectories' regardless of
  source order.
* scifemm/CMakeLists.txt now actually builds scilink.dll (was building
  an unused static library, even though scilink.cpp already declared
  proper dllexports for the three functions scifemm.sci loads via
  link(..., "scilink.dll", ...)) -- the Scilab interface is now
  functional. Added Octave wrappers (octavefemm/mfiles/) for three
  Lua commands that had none: mi_setredraw.m, mi_setgpuaccel.m,
  get_solve_stats.m.
* register_femm_com.ps1 now snapshots whatever COM registration
  existed before it runs (to $env:TEMP, first call of a session only,
  so switching builds mid-session doesn't lose the true original); new
  scripts/unregister_femm_com.ps1 restores it afterwards, or removes
  the registration entirely if there wasn't one. Wired into
  .github/workflows/ccpp.yml as an always-run cleanup step, so running
  the regression suite no longer leaves a permanent side effect on the
  machine's registry.
* Fixed the CPU/GPU load monitor not updating during an interactive
  (GUI "Analyze" click, not scripted) solve: MarkSolveStart()/
  MarkSolveEnd() were only ever called from each analyze entry point's
  Lua-scripted branch, since the interactive path fires off the solver
  process and returns immediately with no wait loop to call
  MarkSolveEnd() from. MarkSolveStart() now optionally takes the
  child process handle, and CLoadMonitorDlg's existing sample timer
  polls a duplicate of it to detect completion and end the solve
  itself (femm/LoadMonitorDlg.h/.cpp, femm/FemmeView.cpp,
  femm/hdrawView.cpp, femm/cdrawView.cpp, femm/beladrawView.cpp).
* Reworded the GPU-accelerated-solve failure dialog (fkn/spars.cpp,
  fkn/cspars.cpp): it said "no usable GPU was found" even when the GPU
  ran fine but the Jacobi preconditioner (needed for GPU parallelism,
  weaker than the CPU solver's SSOR on some matrices, e.g. finely-
  stranded litz windings) simply didn't converge -- now names
  non-convergence as a distinct third cause, and fkn/spars_cuda.cu logs
  which of the three actually happened.
* Added the same dark theme toggle the magnetics editor has to the
  results/post-processor window (CFemmviewView, the .ans window):
  "Dark Theme" on its View menu (IDR_FEMMVIEWTYPE), with dark
  equivalents for the plot-specific colors (region/text/flux-line/
  vector) the editor doesn't have (femm/FemmviewView.h/.cpp,
  femm/femm.rc).
* Batched the density plot's GDI drawing (femm/FemmviewView.cpp):
  PlotFluxDensity issued one Polygon() call per rendered sub-triangle,
  easily millions for a fine mesh, since the per-element field math is
  cheap and GDI call count -- not computation -- is what actually
  dominates a large density-plot repaint. The existing per-legend-band
  (0-19) cached pens/brushes are now view members instead of
  function-local statics, and OnDraw accumulates each band's
  sub-triangle vertices and flushes them with a single PolyPolygon()
  call per band (at most 20 GDI calls per repaint) instead of drawing
  each sub-triangle immediately.

22Oct2023

* Updated radiation boundary condition in thermal problems so that convective
  terms are also used, i.e. a combined radiation/convection/flux boundary can
  be modeled if desired. Convection bc also updated so that convection/flux
  can be modeled if desired.
* Updated some low-level aspects how femm and the solvers are initialized to
  support programmatically hiding the main window while using pyfemm in Wine.
* Patched triangle source code from:
  https://github.com/microsoft/vcpkg/tree/master/ports/triangle
  added for VS2022 compatibility.  The VS2022 version of the solution points
  to the triangle64 directory which contains the patched code.  However, the
  distribution executables were compiled with VS2008 using the source in the
  (unpatched) triangle directory for Wine compatibility.

25Jul2022

* Fixed a bug where imaginary part of a conductor voltage in a current flow
  problems was ignored.

01Apr2022

* Fixed bug where rotation of magnetic material is incorrect if the material
  is defined more than once.

01Mar2022

* Changed installer for better silent operation

23Jan2022:

* Fixed magnetic material property definitions for some SVG wire sizes

26Jun2021:

*  mi_setcomment and related ei_, hi_, and ci_ functions added to set the
   comment text in the problem definition.

26Jun2021:

* Fixed energy integral for airgap boundary conditions.  It didn't work
  correctly for time-harmonic problems (thanks Nicola Bianchi)
* Fixed broken magnetic materials library entries for legacy NdFeB 40, 
  M47 silicon steel.
* Changed lua implementation of mi_probdef and friends so that an out-of-range
  Precision is clipped to the allowable range rather than throwing a cryptic
  error message.
* Fixed SciFEMM to work with Scilab 6.1.0. SciFEMM bug with mi_getmaterial,
  ci_getmaterial, ei_getmaterial, and hi_getmaterial fixed.
* Added retries to various "fopen" instances that could occasionally fail
  in long batch runs.
* Changed formatting string for B-H points dialog to %.15g instead of %f so
  that precision of B-H curve points is not truncated.
* Updated to compile with Visual Studio 2019.  Improves solution speed on
  most problems.
* Fixed OnDraw functions so that the operating system does not think that the
  program is non-responsive during the drawing of density plots for solutions
  with lots of elements.
* Added log(|B|) density plot for magnetics problems.
* Fixed vector plot problem in 22Jul2020 build inadvertently introduced by
  log(|B|) density plot
* Changed all calls to Triangle to use the -j flag to "jettison vertices that
  are not part of the final triangulation from the output .node file". This
  gets rid of the spurious "singlarity flag tripped" and "can't solve the
  problem" messages that popped up due to orphaned nodes in the solution 
  domain.

21Apr2019: 

* Start from previous solution for magnetics problems. To use this, specify a 
  file name in "Previous Solution" edit box in the Problem Definition dialog.
  Specify the "Prev Type" to be "None".  This works if the mesh has not changed
  from the last solution.  Good rotating problems with the "sliding band"
  boundary connecting the rotor and stator.  These problems often involve
  consideration of a sequence of problems with slightly different rotor angles
  and currents.  The "sliding band" approach lets the rotor move without
  modifying the mesh.
* Updated materials library for heat flow problems (thanks to Daniel Gheorghe)
* Updated hard magnetic materials in magnetics materials library (thanks to 
  Mike Devine). Includes new definitions for Sintered NdFeB, Bonded NdFeB,
  Sintered SmCo, and Alnico. See explanatory webpages on femm.info for sources
  and rationale.
* Fixed precision in exported DXF so that no precision is lost in DXF exports.
* Changed the algorithm that identifies limits for plotting flux density so 
  that exterior regions (both Kelvin Transformation and IABC) are not 
  considered. These exterior regions can have high flux densities that mask
  the flux density variations in the interior region of interest.
  
06Oct2018: (Test Build)

* Fixed the cause of an occasional error that happens running many runs in
  Mathematica.  Fkn.exe occasionally could not open a *.ans file to write over 
  older results.  The fix adds a retry if the file can't be opened.
* Fixed a bug with the computation of the "sliding band" incremental torque
  integral. The "previous solution" used to calculate the integral was not
  obtained correctly.
* For incremental runs, mesh temporary files never got deleted, leaving extra,
  spurious files hanging around. Code has been added to get rid of these
  files when they're not needed any more.  

25Feb2018:

* Added an "air gap boundary condition" that allows the rotor to be continuously
  moved without the mesh changing with rotor position. A number of new scripting
  functions have been added to calculate forces, torques, and field values of
  associated with an analytical solution in the air gap between the rotor and
  stator. Detailed examples to follow.
* Added file-by-file smartmesh attribute along with scripting commands for each
  input file type (mi_smartmesh, ei_smartmesh, hi_smartmesh, ci_smartmesh)
  to turn smartmesh on or off for a particular file.  The global smartmesh
  scripting function still works to turn smartmesh on or off across all problem
  types for a particular session.
* Moved to console version of Triangle 1.6 made from directly compiling Triangle
  source rather than wrapping it with a dialog.

03Dec2017: (Test Build)

* Fixed one more focus-stealing hole that took away focus during Lua
  script execution. As noted at https://tinyurl.com/y9uggja3 the OnInitDialog
  functions of triangle.exe, fkn.exe, belasolv.exe, csolv.exe, and hsolve.exe
  must return FALSE instead of TRUE, or else focus might be stolen.
  
24Nov2017: (Test Build)

* Changed to Triangle 1.6 instead of Triangle 1.3.  Version 1.6 is more
  robust and usually does not hang / crash if there are small angles
  in the input geometry.
* In cases where Triangle previously hung (displaying an error message)
  when an error occurred, the program now terminates and indicates that
  an error has occurred.  This is especially useful for Matlab/Octave
  scripts, which now get passed the error indication rather than having
  the script hang on the meshing error. 
* Fixed a number of additional instances of focus-stealing during script
  execution.  This makes it possible to do other things while FEMM is
  running a long (Matlab/Octave or Mathematica) script.
* Added a new optional parameter to openfemm in Matlab/Octave/Mathematica
  and Scilab scripting.  openfemm(1) starts FEMM fully minimized.  
* Calls to the analysis (mi_analyze, etc.) assume the state of the main 
  window.  For example, if a scripting run is started minimized with 
  openfemm(1), subsequent mi_analyze() calls will also be run
  minimized automatically.
* Fixed SciFEMM interface so that it works with Scilab 6.0.0

24Sep2017: (Test Build)

* Fixed mi_attachdefault, ei_attachdefault, hi_attachdefault, and
  ci_attachdefault functions, which didn't work correctly.

23Jun2016: (Test Build)

* Fixed sign error with off-diagonal term in permeability tensor for
  incremental permeability problems.

14Jun2016: (Test Build)

* Added a "frozen permeability" problem type that can be used to split up 
  field contributions between various excitation sources for nonlinear DC
  problems.  Also extends the "incremental permeability" formulation to DC
  problems.  The DC incremental results can be used as part of a general 
  time-transient solver, essentially providing the derivatives of flux
  linkage with respect to incremental changes in current and with 
  respect to small changes in position / orientation of moving parts.
* Dropped support for versions of Mathematica before version 5.0.  Versions
  prior to 5.0 do not include .NET/Link for interprocess communications,

12Jan2016:

* Fixed bug added in 01Nov2015 that messed up the reported permeability and
  field intensity for nonlinear time harmonic problems.

01Nov2015:

* Adds "incremental permeability" AC solver. An example application for
  this functionality is the analysis of the frequency-dependent impedance
  of speaker drivers about the DC operating point established by the 
  speaker's permanent magnet.
  - DC operating point specified as the "Previous Solution" in the problem
    definition or via the mi_setprevious(filename) command in Lua.
* Adds 10% and 15% Copper Clad Aluminum magnet wire material types. The
  material definition is intended to provide accurate estimates of proximity
  and skin effect losses across a wide range of frequencies with a bulk
  wire model (i.e. not every turn has to be individually modeled).
* Changes to some of the issues pointed out over the last couple years, e.g.
  - Change to InTriangle test to fix the issues that could occur if a 
    specified point is exactly on the line between two elements
  - Lua command to programmatically turn off "smart mesh" with the 
    smartmesh(state) Lua command.  State is 0 for no "smart mesh" and 1
	for "smart mesh".  Function has the same name in the Matlab interface
	and is named SmartMesh in the Mathematica implementation
  - Fixed bug where "mo_zoom", "eo_zoom", etc. didn't work right
  - Fixed an issue where the right energy / flux linkage was not reported
    for wound coils in AC magnetic problems if the frequency is very 
	small (e.g. <1μHz)
* For increased compatibility with Mathematica 10, the Mathematica interface
  has been changed to use .NET/Link when it is available (instead of
  MathLink).  .NET/Link invokes FEMM as an out-of-proc ActiveX server 
  (the same way that FEMM communicates with Matlab).

15Nov2013:

* Changed the way that errors are trapped in Matlab/Octave and Scilab
  implementations so that errors that would normally display as message
  boxes in a normal GUI session instead get returned as errors to
  Matlab/Octave or Scilab.  Errors can then be trapped, e.g. by using a
  try/catch block.

07Nov2013:

* Fixed instances of GetWindowLong and SetWindowLong which caused the x64
  build to crash when running on Linux via Wine.
* Fixes to eo_blockintegral and co_blockintegral functions. Previously wouldn't 
  allow integration (e.g. for Weighted Stress Tensor force) if the only selected 
  area was a conductor surface.

25Aug2013:

* Changed IABCs to support either a Dirichlet or Neumann outer edge.  This
  is useful for electrostatic problems.

04Aug2013:

* Added mi_selectcircle, mi_selectrectangle and friends to programmatically
  select regions.
* Changed .dxf import so that objects assigned layers are imported as
  grouped being in the same group.
* Added new "Improvised Asymptotic Boundary Condition" button and mi_makeABC
  and friends Lua functions as an alternate way of solving unbounded
  problems.

11Apr2012:

* Can turn off "smart meshing" via a Preferences selection on the "General Attributes"
  tab by unchecking "use smart meshing"
* Fixed a newly introduced bug where an erroneous resistive loss is computed for AC
  problems in regions where conductivity = 0
* Fixed mi_readdxf problem described as Bug 18 in the bug tracker.

01Oct2011:

* Fixed error in reported flux linkage. Flux linkage for stranded regions 
  carrying zero current is not reported correctly for AC problems.
* The Lua "format" command did not work properly with complex number--it 
  stripped off the imaginary part of the number.  This is now fixed.
* The units reported for some heat flow block integral results were erroneous. 
  This has now been rectified.
* 64-bit version of FEMM 4.2 is now available.
* FEMM has been modified to allow multiple instance of FEMM to run at the 
  same time via ActiveX.  For example, This allows multiple instance of FEMM
  to be controlled by one instance of Matlab or Octave.
* FEMM 42 09Nov2010 asks for Mathematica integration when using the silent 
  install method. The installer script has been modified so that the silent
  install assumes that Mathematica is not available, letting the installation
  complete without requiring operator intervention.
* Default material.  A feature has been added which allows one block label to
  be designated as the default block label. Any unlabeled blocks are then
  assumed to be tagged by the default block label.
* In the current flow problem type, line plots of quantities normal and
  tangential to a user-define contour were messed up because the normal
  and tangential directions were computed incorrectly.  This is now fixed.
* The "default" mesh size has been changed.  In previous builds, using the
  default mesh size nearly always resulted in a mesh that was too coarse to
  give accurate results. The default mesh size has been changed so that
  specifying the default mesh size is adequate for most applications.
  Note: can use the <F3> and <F4> keys to uniformly refine and coarsen the
  mesh for the entire model with one keystroke.
* Added automatic refinement of the mesh near corners.  This refinement 
  improves convergence of results like force, stored energy, etc.
* Changed the way that the maximum flux density is computed for flux density
  plotting purposes.  With the automatic refinement of corners, small elements
  with high flux densities can appear in corners. The modified algorithm
  discounts these small corner elements when picking a maximum for the purposes
  of picking plot contours.
* Changed the key that is used to break out of Lua scripts to ESC from BREAK.
  Many keyboards don't have a BREAK key anymore, so it made sense to make this
  change.
* Changed the selection rectangle to a dotted line so that it would 
  render faster.
* Modified the DXF import to understand closed POLYLINE entities.  Previously,
  only open POLYLINE entities were supported.
* Fixed problem with functionality that creates rounded corners (i.e.
  the functionality invoked by mi_createradius) where the program would not
  allow a radius to be created if the intersection was between a line
  segment and an arc segment if the line segment laid along a ling that
  passed through the center of the circle associated with the arc segment.
* Default install directory changed to c:\femm42 to avoid directory
  permissions problems in Windows 7.
  
09Nov2010:

* Added a set of Scilab functions for interfacing with FEMM. These
  functions were tested using Scilab 5.2.2. The descriptions of these
  functions are identical to those described in the OctaveFEMM
  documentation. Example Scilab scripts are in the femm42/examples directory
  and have a .sce file extension and can be run by typing:
  exec('examplename.sce',-1)
  at the Scilab commandline. If you did not install FEMM to the default
  c:\Program Files\femm42 directory, you'll need to change the first line
  of the *.sce files that loads the FEMM library so that it points to the
  correct library location.

11Oct2010:

* Fixed bug in values of |H| reported in the Output Window for time harmonic
  magnetic problems.
* Fixed bug where in some plots, units of H given as A/m^2 instead of A/m
* Fixed error in mo_showvectorplot Matlab/Octave function.  Also fixed
  similar errors in co_showvectorplot, ho_showvectorplot, eo_showvectorplot
* Fixed messed-up definitions of the Lua functions ei_defineouterspace,
  ei_attachouterspace, and ei_detachouterspace
* Installer now prompts for whether or not Mathetmatica support is to be
  included. If Mathematica support is selected, a version of FEMM is
  installed that assumes the availability of ML32I2.DLL, a DLL installed by
  Mathematica. Otherwise, a version of FEMM is installed that doesn't need
  the Mathlink DLL.
* Re-wrote the GetIntersection routine that finds intersections between two
  line segments.  In some uncommon circumstances, the routine could create 
  extra points when the geometry was moved or rotated.
* Added extra Lua functions mi_getmaterial, ei_getmaterial, hi_getmaterial,
  and ci_getmaterial to fetch material definitions from the materials
  library on disk.  Analogous functions were also added to the Matlab/Octave
  and Mathematica interfaces.

02Nov2009:

* Added the Lua commands mi_setgroup, ei_setgroup, hi_setgroup, ci_setgroup
  that assign all selected items to the group number specified by the
  argument to the function.
* Fixed a bug that caused an incorrect permeability to be reported for
  nonlinear materials at points where the flux density is less than 10nT.
* Fixed bugs with ci_addconductor and ci_modifyconductor Lua functions.
* Fixed bug with CIAddMaterial function in MathFEMM

15Jul2009: Several minor changes have been made versus the 01Apr2009 release:

* Added the following Lua commands that allow direct access to finite
  element mesh information:
  mo_numnodes, mo_numelements, mo_getnode, mo_getelement,
  eo_numnodes, eo_numelements, eo_getnode, eo_getelement,
  ho_numnodes, ho_numelements, ho_getnode, ho_getelement,
  co_numnodes, co_numelements, co_getnode, co_getelement.
  There are Matlab/Octave and Mathematica analogs of these commands, too.
* Made a few more performance tweaks to the Mathematica interface.
* Fixed bug in computation of heat flux passing through a constant
  temperature-type "conductor property"
* Included a new selection of soft magnetic materials in the magnetic 
  materials library. The BH curves for these materials were obtained by
  digitizing the curves picutured in Figure 17, "Direct current 
  magnetization curves for various magnetic materials", Metals Handbook,
  8th ed, Vol. 1, p. 792.  These curves represent a wide variety of
  materials, and the curves are defined to very high flux levels at
  which all materials are fully saturated.
