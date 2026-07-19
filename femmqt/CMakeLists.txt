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

find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

qt_add_executable(femmqt WIN32
  main.cpp
  MainWindow.h
  MainWindow.cpp
  FemmProblem.h
  FemmFileIO.h
  FemmFileIO.cpp
  FemmProblemEdit.h
  FemmProblemEdit.cpp
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
)

target_compile_options(femmqt PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/permissive->)
target_link_libraries(femmqt PRIVATE Qt6::Widgets)

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
