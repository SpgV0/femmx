# Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-18:
# new Qt6-based GUI, Phase 1 (magnetics-only MVP). Purely additive: this
# directory's language standard/compiler options are scoped to this target
# only and never affect femm/ or fkn/'s build, which have no CXX_STANDARD
# set at all and rely on MSVC's legacy default language mode (confirmed
# this session as the reason <chrono>'s STL header fails to compile there
# with cascading C2059/C2143 errors -- this target deliberately opts into
# a real C++17 mode instead of fighting that).
cmake_minimum_required(VERSION 3.21)
project(femmqt CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6 (6.11.1, MSVC 2022 64-bit kit) is installed at this path via the
# official Qt Online Installer -- no vcpkg/package manager involved,
# matching how find_package(MFC REQUIRED) elsewhere in this repo already
# assumes a pre-installed toolkit. Override with -DFEMMQT_QT6_DIR=... if
# Qt lives somewhere else.
set(FEMMQT_QT6_DIR "C:/Qt/6.11.1/msvc2022_64" CACHE PATH "Path to the Qt6 MSVC kit root")
list(APPEND CMAKE_PREFIX_PATH "${FEMMQT_QT6_DIR}")

# Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-19:
# was find_package(Qt6 REQUIRED ...) -- that hard-fails the ENTIRE CMake
# configure (not just this target) when Qt6 isn't installed, which broke
# CI: both ccpp.yml jobs run build.ps1 with -ForceTriangle32bit, whose
# main x64 configure pass (build.ps1's build_win_release64_notriangle
# branch) builds this directory too, and neither CI runner has Qt6
# installed. Since this target is meant to be purely additive (see the
# plan doc), a missing optional dependency should skip just this target,
# not take down femm/fkn/triangle/the installer with it. QUIET + an
# explicit found-check + early return() achieves that; REQUIRED does not
# offer a way to make just the package-not-found case non-fatal.
find_package(Qt6 QUIET COMPONENTS Widgets Svg PrintSupport OPTIONAL_COMPONENTS OpenGLWidgets Test)
# qt_standard_project_setup() (below) enables AUTOMOC but, confirmed this
# session, not reliably AUTORCC -- icons.qrc silently never got compiled
# into a qrc_icons.cpp without this, so IconTheme::themedToolIcon's
# QFile::open(":/icons/...") failed at runtime and every toolbar button
# fell back to Qt's built-in "no icon set" behavior (plain text, no icon
# space reserved) instead of erroring loudly. Set explicitly rather than
# relying on it being a qt_standard_project_setup() default.
set(CMAKE_AUTORCC ON)
if (NOT Qt6_FOUND)
  message(WARNING "femmqt: Qt6 (Widgets, Svg, PrintSupport) not found under CMAKE_PREFIX_PATH or FEMMQT_QT6_DIR="
    "\"${FEMMQT_QT6_DIR}\" -- skipping the femmqt (Qt GUI) target. "
    "Install Qt6 there, or pass -DFEMMQT_QT6_DIR=<path to a Qt6 MSVC kit>, to build it.")
  return()
endif()
qt_standard_project_setup()

# Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-14.
#
# WHERE QT ACTUALLY IS, as opposed to where FEMMQT_QT6_DIR guesses.
#
# FEMMQT_QT6_DIR is a HINT: it is appended to CMAKE_PREFIX_PATH above so
# find_package can find a local Online Installer kit without being told.
# It is not an answer. CI installs Qt somewhere else entirely
# (jurplel/install-qt-action unpacks into the workspace and exports
# CMAKE_PREFIX_PATH), so find_package succeeds against THAT kit while
# FEMMQT_QT6_DIR still points at a C:/Qt path the runner does not have.
#
# Reading the plugin and tool paths out of FEMMQT_QT6_DIR therefore
# worked locally and failed on CI -- and failed late, at the
# qoffscreen.dll existence check, with a message blaming a missing
# plugin when the real problem was that it was looking in the wrong
# installation. Derive the prefix from the package that was found:
# Qt6_DIR is <prefix>/lib/cmake/Qt6, which holds in every kit layout.
if(QT6_INSTALL_PREFIX)
  set(FEMMQT_QT_PREFIX "${QT6_INSTALL_PREFIX}")
else()
  get_filename_component(FEMMQT_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
endif()
message(STATUS "femmqt: using the Qt6 kit at ${FEMMQT_QT_PREFIX}")

# Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-10:
# split into a femmqt_core static library plus a thin executable, so the
# app's own code can be linked into a QTest binary (issue #11). Before
# this there was no enable_testing(), no add_test() and no Qt6::Test
# anywhere in the tree -- CI compiled femmqt and stopped. Most of the
# interesting logic (ConstraintSolver, DxfIO, the FileIO classes,
# MeshBuilder) has no GUI dependency and is directly unit-testable once
# it is reachable from something other than main().
#
# Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-12:
# icons.qrc is NOT in this library. It used to be, with the comment
# "Qt6 emits the resource-initialiser for a qt_add_library target and
# links it in automatically, so both femmqt.exe and the test binary get
# the icons without a manual Q_INIT_RESOURCE call in each."
#
# That is false for a STATIC library, and it is the reason every toolbar
# button rendered as bare text (issue #57). AUTORCC compiled the .qrc
# into a qrc_icons.cpp inside this archive, and the linker then discarded
# that object because nothing references its initialiser -- so ":/icons/"
# simply did not exist at runtime. Measured with a test that links this
# library and asks Qt: 53 of 53 icons absent.
#
# The resource now belongs to each binary that needs it (femmqt below,
# and femmqt_icon_tests), where an executable's own resources are always
# initialised. Compiling it into two separate executables is not
# duplication in any harmful sense -- they are separate images.
qt_add_library(femmqt_core STATIC
  MainWindow.h
  MainWindow.cpp
  FemmProblem.h
  FemmFileIO.h
  FemmFileIO.cpp
  FemmProblemEdit.h
  FemmProblemEdit.cpp
  ConstraintSolver.h
  ConstraintSolver.cpp
  ConstraintListDialog.h
  ConstraintListDialog.cpp
  DenseMatrix.h
  GeometryScene.h
  GeometryScene.cpp
  MeshBuilder.h
  MeshBuilder.cpp
  SolveRunner.h
  SolveRunner.cpp
  MeshSolution.h
  AnsFileIO.h
  AnsFileIO.cpp
  AnsxFileIO.h
  AnsxFileIO.cpp
  SolutionView.h
  SolutionView.cpp
  GuiSwitch.h
  GuiSwitch.cpp
  FileRouting.h
  FileRouting.cpp
  DemoBrowserDialog.h
  DemoBrowserDialog.cpp
  DemoLibrary.h
  DemoLibrary.cpp
  Notify.h
  Notify.cpp
  PlotStateArgs.h
  PlotStateArgs.cpp
  SketchFileIO.h
  SketchFileIO.cpp
  SnapEngine.h
  SnapEngine.cpp
  TrimExtend.h
  TrimExtend.cpp
  OffsetChamfer.h
  OffsetChamfer.cpp
  ConstructionGeometry.h
  ConstructionGeometry.cpp
  SketchTransform.h
  SketchTransform.cpp
  ProblemKind.h
  ProblemKind.cpp
  FemmTextFormat.h
  FemmTextFormat.cpp
  PropertyCodec.h
  PropertyCodec.cpp
  ProblemFileIO.h
  ProblemFileIO.cpp
  PropertyFields.h
  PropertyFields.cpp
  SolutionFileIO.h
  SolutionFileIO.cpp
  SolutionField.h
  SolutionField.cpp
  SolutionIntegrals.h
  SolutionIntegrals.cpp
  SolutionAdapter.h
  SolutionAdapter.cpp
  ConductorAnalysis.h
  ConductorAnalysis.cpp
  PropertyEditDialog.h
  PropertyEditDialog.cpp
  FemxFileIO.h
  FemxFileIO.cpp
  ProblemPropertiesDialog.h
  ProblemPropertiesDialog.cpp
  PropertyListDialog.h
  PropertyListDialog.cpp
  PointPropDialog.h
  PointPropDialog.cpp
  BoundaryPropDialog.h
  BoundaryPropDialog.cpp
  CircuitPropDialog.h
  CircuitPropDialog.cpp
  MaterialPropDialog.h
  MaterialPropDialog.cpp
  MoveCopyDialog.h
  MoveCopyDialog.cpp
  NodePropDialog.h
  NodePropDialog.cpp
  SegmentPropDialog.h
  SegmentPropDialog.cpp
  ArcPropDialog.h
  ArcPropDialog.cpp
  BlockLabelPropDialog.h
  BlockLabelPropDialog.cpp
  IconTheme.h
  IconTheme.cpp
  GeometryView.h
  GeometryView.cpp
  ViewPanning.h
  MeshOverlay.h
  MeshOverlay.cpp
  MeshOverlayItem.h
  MeshOverlayItem.cpp
  AppTheme.h
  AppTheme.cpp
  AppPreferences.h
  AppPreferences.cpp
  PreferencesDialog.h
  PreferencesDialog.cpp
  ExteriorRegionDialog.h
  ExteriorRegionDialog.cpp
  LoadMonitorDialog.h
  LoadMonitorDialog.cpp
  MaterialLibraryIO.h
  MaterialLibraryIO.cpp
  MaterialLibraryDialog.h
  MaterialLibraryDialog.cpp
  OpenBoundaryDialog.h
  OpenBoundaryDialog.cpp
  DxfIO.h
  DxfIO.cpp
  BHCurveDialog.h
  BHCurveDialog.cpp
  CircuitAnalysis.h
  CircuitAnalysis.cpp
  HoverTooltip.h
  HoverTooltip.cpp
  DensityPlotOptionsDialog.h
  DensityPlotOptionsDialog.cpp
  ContourPlotOptionsDialog.h
  ContourPlotOptionsDialog.cpp
  PlotXYChartWidget.h
  PlotXYChartWidget.cpp
)

target_include_directories(femmqt_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(femmqt_core PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/permissive->)
target_link_libraries(femmqt_core PUBLIC Qt6::Widgets Qt6::Svg Qt6::PrintSupport)

qt_add_executable(femmqt WIN32
  main.cpp
  # Here, not in femmqt_core -- see the note above the library (#57).
  icons.qrc
)

# Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
# femmqt.rc embeds FEMMX's app icon (femm/res/idr_mainframe.ico, same one
# script.nsi's installer now uses) as the exe's own Windows icon -- without
# it, Explorer/taskbar/Alt+Tab/the window's title-bar corner all show a
# generic blank icon, since qt_add_executable() doesn't add one on its own.
# Only added on Windows -- .rc is an MSVC/rc.exe resource script, not
# portable, and this target is Windows-only anyway (WIN32 above).
if(WIN32)
  target_sources(femmqt PRIVATE femmqt.rc)
endif()

target_compile_options(femmqt PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/permissive->)
target_link_libraries(femmqt PRIVATE femmqt_core)

# Mirrors femm/CMakeLists.txt's identical block: femmqt.exe itself never
# touches CUDA (it only shells out to fkn.exe/hsolv.exe/belasolv.exe/
# csolv.exe), but it still needs to know at compile time whether the
# solver executables it'll be packaged/installed alongside were built with
# CUDA support, so FemmProblem's default GPUAccel can match -- defaulting
# GPUAccel on for a non-CUDA build would pop up "built without CUDA
# support" on every single solve (see fkn/spars.cpp), not just the first.
if(ENABLE_CUDA_SOLVER)
  target_compile_definitions(femmqt_core PUBLIC FEMM_CUDA_ENABLED)
endif()

# Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-20:
# per user request to prioritize pan/zoom responsiveness on large meshes
# via GPU-accelerated rendering -- SolutionGraphicsView sets a QOpenGLWidget
# as its viewport (see that class's constructor) when this module is
# available, offloading rasterization (the density plot's triangle fills
# in particular) to the GPU instead of QPainter's software raster backend.
# OPTIONAL_COMPONENTS above means this module's absence doesn't take the
# whole target down (matches this file's existing missing-Qt6 tolerance) --
# FEMMQT_HAVE_OPENGL gates the actual QOpenGLWidget usage in SolutionView.cpp.
if(TARGET Qt6::OpenGLWidgets)
  target_link_libraries(femmqt_core PUBLIC Qt6::OpenGLWidgets)
  # TEMP DIAGNOSTIC -- disabled to test a blue-background rendering bug
  # target_compile_definitions(femmqt PRIVATE FEMMQT_HAVE_OPENGL)
endif()

# Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-10:
# femmqt's first automated test target (issue #11). Guarded exactly the way
# the Qt6 lookup above is: Test is an OPTIONAL_COMPONENT, so a Qt kit without
# it -- or no Qt at all -- skips this quietly instead of failing the whole
# configure, which is what broke CI when the Qt6 lookup was REQUIRED.
if(TARGET Qt6::Test)
  qt_add_executable(femmqt_tests
    tests/tst_femmqt_core.cpp
  )
  qt_add_executable(femmqt_constraint_tests
    tests/tst_constraint_solver.cpp
  )
  qt_add_executable(femmqt_geometry_tests
    tests/tst_geometry_edit.cpp
  )
  # femm.cfg is shared with the classic GUI and with AppPreferences, and
  # each writer knows only its own keys (issue #19).
  qt_add_executable(femmqt_gui_switch_tests
    tests/tst_gui_switch.cpp
  )
  # Create Radius rewrites topology rather than moving entities about, so
  # it fails differently from everything tst_geometry_edit covers (#24).
  qt_add_executable(femmqt_create_radius_tests
    tests/tst_create_radius.cpp
  )
  # Arcs derive centre/radius/sweep from two endpoints plus an angle, so
  # they behave differently from every other entity under mirroring (#25).
  qt_add_executable(femmqt_arc_tests
    tests/tst_arc_geometry.cpp
  )
  # The layer between the user and ConstraintSolver -- hit-test payloads,
  # glyph selection, deletion. QTEST_MAIN, so it needs the offscreen QPA
  # plugin the deploy step below provides (#26).
  qt_add_executable(femmqt_sketch_ui_tests
    tests/tst_sketch_ui.cpp
  )
  # The sketch layer's sidecar file, and the reference resolution that
  # makes restoring it safe rather than merely possible (#27).
  qt_add_executable(femmqt_sketch_persistence_tests
    tests/tst_sketch_persistence.cpp
  )
  # Snap target resolution -- mostly about PRIORITY, which is the part
  # that decides whether specific targets are reachable at all (#28).
  qt_add_executable(femmqt_snap_tests
    tests/tst_snap_engine.cpp
  )

  # Trim, extend and split (#29). The arc cases check that each result
  # still lies on the SAME circle as the arc it came from, which is the
  # kind of property a hand-computed expected coordinate cannot verify --
  # see #25, where the hand derivation was wrong the same way the code
  # was.
  qt_add_executable(femmqt_trim_tests
    tests/tst_trim_extend.cpp
  )

  # Offset and chamfer (#30). The offset cases measure DISTANCE and SIDE
  # back against the original, because the failure mode is valid
  # geometry in the wrong place -- which no assertion about entity counts
  # can see.
  qt_add_executable(femmqt_offset_tests
    tests/tst_offset_chamfer.cpp
  )

  # Construction geometry (#31). Written from the outside in: not "the
  # flag is set" but "the bytes that leave this program do not contain
  # it", for all three exits -- the .fem, the .femx cache and the .poly.
  qt_add_executable(femmqt_construction_tests
    tests/tst_construction.cpp
  )

  # What transforms do to the sketch layer (#32). Half of it pins down
  # the audit's finding -- references were already correct -- so that
  # staying correct is checked rather than assumed.
  qt_add_executable(femmqt_sketch_transform_tests
    tests/tst_sketch_transform.cpp
  )

  # Tags the solvers read that no GUI writes, plus the drift guard on
  # .femx's two independent implementations. Both are text scans over the
  # repo, so they need no Qt runtime and no fixtures.
  qt_add_executable(femmqt_solver_tag_tests
    tests/tst_solver_tags.cpp
  )

  # All four model formats through one reader and one writer (#80).
  # Every field of every record is set to a distinct value and compared
  # back, because the failure here is a field silently reading as zero,
  # not a crash.
  qt_add_executable(femmqt_problem_kind_tests
    tests/tst_problem_kinds.cpp
  )

  # The four solution formats (#83). The failure guarded against is a
  # COLUMN SHIFT -- these files are whitespace-separated numbers, so
  # reading the wrong width per node parses cleanly and is wrong down
  # the whole mesh.
  qt_add_executable(femmqt_solution_format_tests
    tests/tst_solution_formats.cpp
  )

  # Derived field quantities (#83). Checked against analytic answers --
  # a linear potential has an exactly constant gradient -- because a
  # field wrong only by a constant factor is invisible on a plot with an
  # auto-scaled legend.
  qt_add_executable(femmqt_solution_field_tests
    tests/tst_solution_field.cpp
  )

  # Point, line and block integrals (#83), every expected number from a
  # closed form rather than a previous run -- an integral wrong by a
  # constant still looks like a number.
  qt_add_executable(femmqt_solution_integral_tests
    tests/tst_solution_integrals.cpp
  )

  # The guard #84 is really about: the CAD layer must stay type-blind.
  # A static scan plus a behavioural comparison across all four types --
  # either alone can be slipped past.
  qt_add_executable(femmqt_type_independence_tests
    tests/tst_type_independence.cpp
  )
  # Drives the real femmqt.exe as a child process, with a hard time cap:
  # the defect it guards is a HANG, and an untimed assertion against a
  # process that never exits does not fail, it hangs too (#85).
  qt_add_executable(femmqt_headless_cli_tests
    tests/tst_headless_cli.cpp
  )
  # The shipped demo library, and the guarantee that opening one never
  # touches it (#89/#91).
  qt_add_executable(femmqt_demo_library_tests
    tests/tst_demo_library.cpp
  )
  # The browser itself -- widgets, so this one needs a QPA platform.
  qt_add_executable(femmqt_demo_browser_tests
    tests/tst_demo_browser.cpp
  )
  # Also drives femmqt.exe: the question "did the render obey the plot
  # state" can only be asked of an actual image (#86).
  qt_add_executable(femmqt_plot_state_tests
    tests/tst_plot_state.cpp
  )
  # The toolbar icon resource. Every button rendered as bare text and
  # nothing said why, because an empty QIcon draws exactly like a
  # deliberately text-only button.
  qt_add_executable(femmqt_icon_tests
    tests/tst_icons.cpp
    # The resource under test. Linked into this binary for the same
    # reason femmqt.exe links it: a static library's copy gets
    # discarded (#57).
    icons.qrc
  )
  # Every test target, named once. These used to be two hand-kept
  # lists and they drifted: the Qt-deployment loop below still named
  # only the original four, so every target added since ran on
  # whatever DLLs happened to be left in the directory -- and a new
  # one failed to start at all, with exit 127 and no message.
  set(FEMMQT_TEST_TARGETS
    femmqt_tests
    femmqt_constraint_tests
    femmqt_geometry_tests
    femmqt_gui_switch_tests
    femmqt_create_radius_tests
    femmqt_arc_tests
    femmqt_sketch_ui_tests
    femmqt_sketch_persistence_tests
    femmqt_snap_tests
    femmqt_trim_tests
    femmqt_offset_tests
    femmqt_construction_tests
    femmqt_sketch_transform_tests
    femmqt_solver_tag_tests
    femmqt_problem_kind_tests
    femmqt_solution_format_tests
    femmqt_solution_field_tests
    femmqt_solution_integral_tests
    femmqt_type_independence_tests
    femmqt_headless_cli_tests
    femmqt_demo_library_tests
    femmqt_demo_browser_tests
    femmqt_plot_state_tests
    femmqt_icon_tests
  )
  foreach(_femmqt_test ${FEMMQT_TEST_TARGETS})
    target_compile_options(${_femmqt_test} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/permissive->)
    target_link_libraries(${_femmqt_test} PRIVATE femmqt_core Qt6::Test)
    # Fixtures live in the repo's test/ tree, two levels up. Passed as a
    # define rather than found with QFINDTESTDATA, which searches the
    # build tree and the app directory and would not look there.
    target_compile_definitions(${_femmqt_test} PRIVATE
      FEMMQT_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../test/fixtures")
    # tst_icons.cpp scans icons.qrc and MainWindow.cpp as TEXT, so it
    # needs the source directory as well as the fixture one.
    target_compile_definitions(${_femmqt_test} PRIVATE
      FEMMQT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  endforeach()

  # Never installed and never packaged: this is a build-tree artefact.
  set_target_properties(${FEMMQT_TEST_TARGETS} PROPERTIES
    WIN32_EXECUTABLE OFF          # console apps, so QTest output reaches ctest
    EXCLUDE_FROM_ALL OFF
  )

  add_test(NAME femmqt_core COMMAND femmqt_tests)
  add_test(NAME femmqt_constraints COMMAND femmqt_constraint_tests)
  add_test(NAME femmqt_geometry COMMAND femmqt_geometry_tests)
  add_test(NAME femmqt_gui_switch COMMAND femmqt_gui_switch_tests)
  add_test(NAME femmqt_create_radius COMMAND femmqt_create_radius_tests)
  add_test(NAME femmqt_arcs COMMAND femmqt_arc_tests)
  add_test(NAME femmqt_sketch_ui COMMAND femmqt_sketch_ui_tests)
  add_test(NAME femmqt_sketch_persistence COMMAND femmqt_sketch_persistence_tests)
  add_test(NAME femmqt_snap COMMAND femmqt_snap_tests)
  add_test(NAME femmqt_trim COMMAND femmqt_trim_tests)
  add_test(NAME femmqt_offset COMMAND femmqt_offset_tests)
  add_test(NAME femmqt_construction COMMAND femmqt_construction_tests)
  add_test(NAME femmqt_sketch_transform COMMAND femmqt_sketch_transform_tests)
  add_test(NAME femmqt_solver_tags COMMAND femmqt_solver_tag_tests)
  add_test(NAME femmqt_problem_kinds COMMAND femmqt_problem_kind_tests)
  add_test(NAME femmqt_solution_formats COMMAND femmqt_solution_format_tests)
  add_test(NAME femmqt_solution_field COMMAND femmqt_solution_field_tests)
  add_test(NAME femmqt_solution_integrals COMMAND femmqt_solution_integral_tests)
  add_test(NAME femmqt_type_independence COMMAND femmqt_type_independence_tests)
  # This one spawns femmqt.exe rather than linking against it, so the
  # dependency is not implied by the link graph and has to be stated.
  # Without it the test binary can be up to date while the executable it
  # drives is missing or stale -- which fails loudly (initTestCase
  # refuses to run vacuously) but for a confusing reason.
  add_dependencies(femmqt_headless_cli_tests femmqt)
  add_dependencies(femmqt_plot_state_tests femmqt)

  add_test(NAME femmqt_headless_cli COMMAND femmqt_headless_cli_tests)
  add_test(NAME femmqt_demo_library COMMAND femmqt_demo_library_tests)
  add_test(NAME femmqt_demo_browser COMMAND femmqt_demo_browser_tests)
  add_test(NAME femmqt_plot_state COMMAND femmqt_plot_state_tests)
  set_tests_properties(femmqt_plot_state PROPERTIES TIMEOUT 900)
  # Spawns femmqt.exe, which must exist first.
  set_tests_properties(femmqt_headless_cli PROPERTIES TIMEOUT 600)
  add_test(NAME femmqt_icons COMMAND femmqt_icon_tests)
  # Offscreen by construction. No test may require a desktop session: the
  # CI runners are headless, and a widget test on a headless box without
  # this either fails obscurely or hangs. tst_femmqt_core.cpp asserts this
  # variable is actually set, so losing it here fails loudly rather than
  # silently reintroducing a display dependency.
  set_tests_properties(femmqt_core femmqt_constraints femmqt_geometry femmqt_gui_switch femmqt_create_radius femmqt_arcs femmqt_sketch_ui femmqt_sketch_persistence femmqt_snap femmqt_icons femmqt_demo_browser PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 300
  )

  # The test binary needs the Qt runtime beside it for the same reason
  # femmqt.exe does.
  #
  # Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-11
  # (issue #19). Two things a QTest binary needs that windeployqt does not
  # provide, both of which used to surface as a 300-second hang with no
  # output rather than as an error:
  #
  #  1. Qt6Test.dll. windeployqt deploys applications, and Qt excludes the
  #     Test module from deployment on the reasonable grounds that you do
  #     not ship tests -- but these binaries still have to RUN from the
  #     build tree. Measured: by hand the binary says "error while loading
  #     shared libraries: Qt6Test.dll" and exits 127; under ctest the same
  #     failure produced no output at all and a kill at the timeout, with
  #     nothing anywhere naming the missing DLL.
  #
  #  2. qoffscreen.dll, for the targets that use QTEST_MAIN (today
  #     tst_geometry_edit.cpp) and so construct a QApplication, which needs
  #     a QPA platform. windeployqt deploys only qwindows.dll.
  #
  # Both copies now run per target, immediately after that target's own
  # windeployqt step, rather than being attached to one target -- a
  # POST_BUILD command only runs when its target relinks, so a step hung
  # off a single target silently stops maintaining the directory as soon
  # as that one target is up to date. Measured: femmqt_geometry_tests hung
  # for exactly this reason, with platforms/ holding only qwindows.dll.
  #
  # The plugin is a hard requirement, so a missing one is a configure
  # error. Silently skipping it is what turned a missing file into a hang.
  #
  # Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-12:
  # this find_program used to live ~35 lines BELOW here, next to
  # femmqt.exe's own deployment step, and this block simply read the
  # variable it sets. On a re-configure that happened to work, because
  # find_program caches -- but on a CLEAN configure the cache entry did
  # not exist yet, the guard below was false, and the entire test
  # deployment was skipped without a word. The test binaries then had no
  # Qt6Test.dll and no offscreen plugin beside them and exited
  # immediately with no output, which ctest reports as a timeout.
  #
  # Reproduced from a fresh build directory this session. It is the same
  # silent-skip the comment directly above warns about, one level up: the
  # careful FATAL_ERROR for a missing qoffscreen.dll was unreachable,
  # because the `if` guarding it was already false.
  #
  # Modified 2026-09-14: hinted at the FOUND kit's bin directory, not at
  # FEMMQT_QT6_DIR's. Deploying with one kit's windeployqt while linking
  # against another's is a mismatch that produces a runnable-looking
  # build with the wrong DLLs beside it.
  find_program(FEMMQT_WINDEPLOYQT_EXECUTABLE windeployqt
    HINTS "${FEMMQT_QT_PREFIX}/bin" "${FEMMQT_QT6_DIR}/bin")
  if(WIN32 AND NOT FEMMQT_WINDEPLOYQT_EXECUTABLE)
    message(FATAL_ERROR
      "femmqt: windeployqt was not found in \"${FEMMQT_QT_PREFIX}/bin\". "
      "The test binaries need the Qt runtime, Qt6Test.dll and the "
      "offscreen QPA plugin copied next to them, and without it they do "
      "not start at all -- silently, with no output, which ctest reports "
      "as a timeout rather than a missing dependency. Point "
      "-DFEMMQT_QT6_DIR at a complete Qt6 MSVC kit, or disable the Qt "
      "GUI with -DSKIP_femmqt=ON.")
  endif()
  if(WIN32 AND FEMMQT_WINDEPLOYQT_EXECUTABLE)
    # Modified 2026-09-14: ask the found Qt where its offscreen plugin
    # is, rather than assuming FEMMQT_QT6_DIR's layout.
    #
    # Qt ships the QPA plugins as imported targets, so
    # Qt6::QOffscreenIntegrationPlugin resolves to the exact DLL in
    # whichever kit find_package settled on, per configuration. The
    # path fallback is for a kit old or trimmed enough not to define
    # the target -- and it searches the FOUND prefix first, which is
    # the whole point of the change.
    if(TARGET Qt6::QOffscreenIntegrationPlugin)
      set(_femmqt_offscreen "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>")
    else()
      set(_femmqt_offscreen "${FEMMQT_QT_PREFIX}/plugins/platforms/qoffscreen.dll")
      if(NOT EXISTS "${_femmqt_offscreen}")
        set(_femmqt_offscreen "${FEMMQT_QT6_DIR}/plugins/platforms/qoffscreen.dll")
      endif()
      if(NOT EXISTS "${_femmqt_offscreen}")
        message(FATAL_ERROR
          "femmqt: the offscreen QPA plugin is missing from the Qt kit at "
          "\"${FEMMQT_QT_PREFIX}\" (no Qt6::QOffscreenIntegrationPlugin "
          "target, and no plugins/platforms/qoffscreen.dll). The headless "
          "tests cannot run without it, and without this check they would "
          "hang at their ctest timeout rather than fail. Install the Qt "
          "kit's platform plugins, or pass -DFEMMQT_QT6_DIR=<a complete "
          "Qt6 MSVC kit>.")
      endif()
    endif()
    foreach(_femmqt_test ${FEMMQT_TEST_TARGETS})
      add_custom_command(TARGET ${_femmqt_test} POST_BUILD
        COMMAND "${FEMMQT_WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:${_femmqt_test}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::Test>"
                "$<TARGET_FILE_DIR:${_femmqt_test}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_femmqt_offscreen}"
                "$<TARGET_FILE_DIR:${_femmqt_test}>/platforms/qoffscreen.dll"
        COMMENT "Deploying the Qt runtime, Qt6Test and the offscreen QPA plugin next to ${_femmqt_test}.exe"
      )
    endforeach()
  endif()
else()
  message(STATUS "femmqt: Qt6::Test not available -- skipping the femmqt_tests target")
endif()

install(TARGETS femmqt DESTINATION "${INSTALL_BIN_DIR}")

# Copies the Qt runtime DLLs (Qt6Core.dll, Qt6Widgets.dll, Qt6Gui.dll, the
# platforms/qwindows.dll plugin, etc.) next to femmqt.exe after each build,
# so it's directly runnable straight from the build tree (e.g.
# build_qt/femmqt/Debug/femmqt.exe) without hand-copying DLLs or editing
# PATH. This does NOT cover the separately-installed copy in
# INSTALL_BIN_DIR (bin\) -- see the install(SCRIPT ...) below for that.
if(WIN32)
  # Already located above, before the test-deployment block that also
  # needs it -- kept here as a no-op for the SKIP_femmqt_tests path,
  # where that block never runs. find_program is a cache lookup once set.
  find_program(FEMMQT_WINDEPLOYQT_EXECUTABLE windeployqt
    HINTS "${FEMMQT_QT_PREFIX}/bin" "${FEMMQT_QT6_DIR}/bin")
  if(FEMMQT_WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET femmqt POST_BUILD
      COMMAND "${FEMMQT_WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:femmqt>"
      COMMENT "Deploying Qt runtime DLLs next to femmqt.exe"
    )
  endif()
endif()

# Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-18:
# the POST_BUILD windeployqt step above only ever touches the build tree's
# own copy of femmqt.exe -- it has no effect on the separate copy
# install(TARGETS ...) above places in INSTALL_BIN_DIR (bin\), which is
# what build_femmx.ps1 packages into bin\plain\/bin\cuda\ and what
# script.nsi ships. Without this, bin\femmqt.exe exists after `cmake
# --build . --target install` but is missing its required Qt platform
# plugin (bin\platforms\qwindows.dll) and fails to start.
#
# Tried Qt6's own qt_generate_deploy_app_script() first (its documented
# mechanism for this) but it assumes a standard CMAKE_INSTALL_PREFIX-
# relative install layout: it deploys next to the BUILD TREE's copy of
# the exe (from $<TARGET_FILE:femmqt>, since it can't otherwise know
# where an absolute, non-prefix-relative DESTINATION like INSTALL_BIN_DIR
# ends up) while separately trying to write qt.conf under
# CMAKE_INSTALL_PREFIX (this project never sets one, so it defaulted to
# the unwritable "C:/Program Files/FEMM" and failed the install step
# outright). Rather than fight that mismatch, this runs windeployqt
# directly against the actual installed path, matching how
# installer/CMakeLists.txt already invokes makensis via a plain
# install(CODE "execute_process(...)") -- consistent with this codebase's
# existing pattern for install-time external tool invocations.
if(WIN32 AND FEMMQT_WINDEPLOYQT_EXECUTABLE)
  install(CODE "
    execute_process(
      COMMAND \"${FEMMQT_WINDEPLOYQT_EXECUTABLE}\" --no-translations \"${INSTALL_BIN_DIR}/femmqt.exe\"
      RESULT_VARIABLE _femmqt_windeployqt_result
    )
    if(NOT _femmqt_windeployqt_result EQUAL 0)
      message(WARNING \"windeployqt exited with code \${_femmqt_windeployqt_result} -- femmqt.exe in ${INSTALL_BIN_DIR} may be missing its Qt runtime\")
    endif()
  ")
endif()
