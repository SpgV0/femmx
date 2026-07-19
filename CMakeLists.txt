cmake_minimum_required(VERSION 3.10)
project(FEMM C CXX)

# Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-09:
# added the automatic NSIS installer build (installer/CMakeLists.txt), so
# `cmake --build . --target install` (what build.ps1 always runs) also
# produces bin/FEMMX_v<version>_installer.exe (see script.nsi's
# PROJECT_VERSION) whenever makensis is available -- no separate manual
# step needed.
# Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-09:
# moved the NSIS install(CODE) block out into installer/CMakeLists.txt,
# added via add_subdirectory(installer) as the LAST add_subdirectory()
# call below. It used to live directly in this file, after all the
# add_subdirectory() calls -- which looked like it should make it run
# last, but doesn't: CMake orders a directory's own install() rules
# before any add_subdirectory()-included ones regardless of source
# order, so it was actually packaging whatever was already sitting in
# INSTALL_BIN_DIR from an earlier build, not this build's fresh output.
# Sibling subdirectories DO preserve call order relative to each other,
# which is what actually fixes it.

option(BUILD_MANUAL "Build the manual" OFF)
option(SKIP_belasolv "Skip building belasolv" OFF)
option(SKIP_csolv "Skip building csolv" OFF)
option(SKIP_liblua "Skip building liblua" OFF)
option(SKIP_ResizableLib "Skip building ResizableLib" OFF)
option(SKIP_femm "Skip building femm" OFF)
option(SKIP_femmplot "Skip building femmplot" OFF)
option(SKIP_femmqt "Skip building femmqt" OFF)
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
if (NOT SKIP_femmqt)
  add_subdirectory(femmqt)
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

# Must stay the LAST add_subdirectory() call -- see installer/CMakeLists.txt.
add_subdirectory(installer)
