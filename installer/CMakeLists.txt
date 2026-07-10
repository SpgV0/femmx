# Added by Claude (Anthropic), noreply@anthropic.com, 2026-07-09.
#
# Moved here (out of the root CMakeLists.txt) so this runs after every
# other target's install(TARGETS...) rules. CMake orders a directory's own
# install() rules before any add_subdirectory()-included ones, regardless
# of the source's textual order -- add_subdirectory(installer) being the
# LAST add_subdirectory() call in the root CMakeLists.txt is what actually
# makes this run last (subdirectories preserve relative call order among
# each other; verified by inspecting the generated cmake_install.cmake).
# Without this, makensis packaged whatever happened to already be in
# INSTALL_BIN_DIR from a previous build rather than this build's fresh
# output -- easy to miss if INSTALL_BIN_DIR already has leftover files in
# it, which is exactly what masked this for a while.

# Build the NSIS installer (script.nsi) as part of `--target install`,
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
