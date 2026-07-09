cmake_minimum_required(VERSION 3.10)
project(FEMM C CXX)

# Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-09:
# added the automatic NSIS installer build below (after the
# add_subdirectory() calls), so `cmake --build . --target install`
# (what build.ps1 always runs) also produces bin/FEMMX_installer.exe
# whenever makensis is available -- no separate manual step needed.

option(BUILD_MANUAL "Build the manual" OFF)
option(SKIP_belasolv "Skip building belasolv" OFF)
option(SKIP_csolv "Skip building csolv" OFF)
option(SKIP_liblua "Skip building liblua" OFF)
option(SKIP_ResizableLib "Skip building ResizableLib" OFF)
option(SKIP_femm "Skip building femm" OFF)
option(SKIP_femmplot "Skip building femmplot" OFF)
option(SKIP_fkn "Skip building fkn" OFF)
option(SKIP_hsolv "Skip building hsolv" OFF)
option(SKIP_scifemm "Skip building scifemm" OFF)
option(SKIP_triangle "Skip building triangle" OFF)

set(INSTALL_BIN_DIR  "${CMAKE_SOURCE_DIR}/bin" CACHE PATH "Path where exe will be installed")

if(BUILD_MANUAL)
  add_subdirectory(manual)
endif()

if (NOT SKIP_belasolv)
  add_subdirectory(belasolv)
endif()
if (NOT SKIP_csolv)
  add_subdirectory(csolv)
endif()
if (NOT SKIP_liblua)
  add_subdirectory(liblua)
endif()
if (NOT SKIP_ResizableLib)
  add_subdirectory(ResizableLib)
endif()
if (NOT SKIP_femm)
  add_subdirectory(femm)
endif()
if (NOT SKIP_femmplot)
  add_subdirectory(femmplot)
endif()
if (NOT SKIP_fkn)
  add_subdirectory(fkn)
endif()
if (NOT SKIP_hsolv)
  add_subdirectory(hsolv)
endif()
if (NOT SKIP_scifemm)
  add_subdirectory(scifemm)
endif()
if(NOT SKIP_triangle)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_subdirectory(triangle64)
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_subdirectory(triangle)
  endif()
endif()

# Build the NSIS installer (script.nsi) as part of `--target install`,
# once everything above has been copied into INSTALL_BIN_DIR. Silently
# skipped (like the optional LaTeX manual) if makensis isn't found --
# most dev machines won't have NSIS installed, and a partial build
# (any SKIP_* option set) will make makensis itself fail with a clear
# "file not found" error, which is reported as a warning rather than
# failing the whole install step.
find_program(MAKENSIS_EXECUTABLE makensis
  HINTS
    "$ENV{ProgramFiles}/NSIS"
    "$ENV{ProgramFiles\(x86\)}/NSIS"
)
if(MAKENSIS_EXECUTABLE)
  message(STATUS "Found makensis: ${MAKENSIS_EXECUTABLE} -- installer will be built")
  install(CODE "
    execute_process(
      COMMAND \"${MAKENSIS_EXECUTABLE}\" \"${CMAKE_SOURCE_DIR}/script.nsi\"
      WORKING_DIRECTORY \"${CMAKE_SOURCE_DIR}\"
      RESULT_VARIABLE _makensis_result
    )
    if(NOT _makensis_result EQUAL 0)
      message(WARNING \"makensis exited with code \${_makensis_result} -- installer was not built (see script.nsi; this is expected for a partial build, e.g. with a SKIP_* option set)\")
    endif()
  ")
else()
  message(STATUS "makensis not found -- skipping automatic installer build (see script.nsi; CI installs NSIS via choco)")
endif()
