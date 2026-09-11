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

# Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-10:
# split into a femmqt_core static library plus a thin executable, so the
# app's own code can be linked into a QTest binary (issue #11). Before
# this there was no enable_testing(), no add_test() and no Qt6::Test
# anywhere in the tree -- CI compiled femmqt and stopped. Most of the
# interesting logic (ConstraintSolver, DxfIO, the FileIO classes,
# MeshBuilder) has no GUI dependency and is directly unit-testable once
# it is reachable from something other than main().
#
# icons.qrc lives in the LIBRARY rather than the executable: Qt6 emits the
# resource-initialiser for a qt_add_library target and links it in
# automatically, so both femmqt.exe and the test binary get the icons
# without a manual Q_INIT_RESOURCE call in each.
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
  icons.qrc
)

target_include_directories(femmqt_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(femmqt_core PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/permissive->)
target_link_libraries(femmqt_core PUBLIC Qt6::Widgets Qt6::Svg Qt6::PrintSupport)

qt_add_executable(femmqt WIN32
  main.cpp
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
  foreach(_femmqt_test femmqt_tests femmqt_constraint_tests)
    target_compile_options(${_femmqt_test} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/permissive->)
    target_link_libraries(${_femmqt_test} PRIVATE femmqt_core Qt6::Test)
  endforeach()

  # Never installed and never packaged: this is a build-tree artefact.
  set_target_properties(femmqt_tests femmqt_constraint_tests PROPERTIES
    WIN32_EXECUTABLE OFF          # console apps, so QTest output reaches ctest
    EXCLUDE_FROM_ALL OFF
  )

  add_test(NAME femmqt_core COMMAND femmqt_tests)
  add_test(NAME femmqt_constraints COMMAND femmqt_constraint_tests)
  # Offscreen by construction. No test may require a desktop session: the
  # CI runners are headless, and a widget test on a headless box without
  # this either fails obscurely or hangs. tst_femmqt_core.cpp asserts this
  # variable is actually set, so losing it here fails loudly rather than
  # silently reintroducing a display dependency.
  set_tests_properties(femmqt_core femmqt_constraints PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 300
  )

  # The test binary needs the Qt runtime beside it for the same reason
  # femmqt.exe does.
  if(WIN32 AND FEMMQT_WINDEPLOYQT_EXECUTABLE)
    foreach(_femmqt_test femmqt_tests femmqt_constraint_tests)
      add_custom_command(TARGET ${_femmqt_test} POST_BUILD
        COMMAND "${FEMMQT_WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:${_femmqt_test}>"
        COMMENT "Deploying Qt runtime DLLs next to ${_femmqt_test}.exe"
      )
    endforeach()
    # windeployqt deploys the platform plugin it thinks the app needs, which
    # on Windows is qwindows.dll and nothing else. A test running under
    # QT_QPA_PLATFORM=offscreen then cannot load a platform at all: measured,
    # the binary produced no output and ctest killed it at its 300s timeout.
    # The current tests use QTEST_GUILESS_MAIN and need no platform, but the
    # widget tests later tickets add will, so put qoffscreen.dll where they
    # will look for it.
    if(EXISTS "${FEMMQT_QT6_DIR}/plugins/platforms/qoffscreen.dll")
      add_custom_command(TARGET femmqt_constraint_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${FEMMQT_QT6_DIR}/plugins/platforms/qoffscreen.dll"
                "$<TARGET_FILE_DIR:femmqt_constraint_tests>/platforms/qoffscreen.dll"
        COMMENT "Deploying the offscreen QPA plugin for headless tests"
      )
    endif()
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
  find_program(FEMMQT_WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${FEMMQT_QT6_DIR}/bin")
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
