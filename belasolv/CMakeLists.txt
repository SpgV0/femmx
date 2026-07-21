cmake_minimum_required(VERSION 3.10)
project(belasolv C CXX)

set(INSTALL_BIN_DIR  "${CMAKE_SOURCE_DIR}" CACHE PATH "Path where exe will be installed")

find_package(MFC REQUIRED)
set(CMAKE_MFC_FLAG 2)
add_definitions(-D_WIN32_WINNT=0x0501)

set(belasolv_SOURCES
  belasolv.h
  belasolvDlg.h
  femmedoccore.h
  mesh.h
  resource.h
  spars.h
  spars_cuda.h
  StdAfx.h
  belasolv.cpp
  belasolvDlg.cpp
  cuthill.cpp
  femmedoccore.cpp
  main.cpp
  prob1big.cpp
  spars.cpp
  StdAfx.cpp
  belasolv.rc
)

# Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
# optional CUDA-accelerated linear solve (spars_cuda.cu/.h), mirroring
# fkn/CMakeLists.txt's identical block for the magnetics solver.
# ENABLE_CUDA_SOLVER is declared once, at the root CMakeLists.txt (before
# add_subdirectory(belasolv)), so it doesn't need to be redeclared here.
if(ENABLE_CUDA_SOLVER)
  # Same cache variables fkn/CMakeLists.txt declares -- CMake cache vars
  # are global, so whichever of fkn/hsolv/belasolv/csolv's add_subdirectory()
  # runs first actually sets these, but each declares them independently so
  # none depends on another's subdirectory ordering.
  set(FEMM_CUDA_ROOT "" CACHE PATH "CUDA Toolkit root, e.g. \"C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6\"")
  set(FEMM_CUDA_CCBIN "" CACHE PATH "Optional nvcc -ccbin override: an alternate MSVC toolset bin dir, needed if the default one is newer than the CUDA Toolkit supports")
  set(FEMM_CUDA_ARCHS "60;75;86;89" CACHE STRING "CUDA compute capabilities to build for (semicolon-separated, no dots)")
  if(NOT FEMM_CUDA_ROOT)
    message(FATAL_ERROR "ENABLE_CUDA_SOLVER requires -DFEMM_CUDA_ROOT=<path to CUDA Toolkit>")
  endif()

  set(BELASOLV_NVCC "${FEMM_CUDA_ROOT}/bin/nvcc.exe")
  set(BELASOLV_CUDA_OBJ "${CMAKE_CURRENT_BINARY_DIR}/spars_cuda.obj")

  # Match the MSVC runtime library the rest of the (MFC) project is built
  # with -- see fkn/CMakeLists.txt's identical comment for the full
  # rationale (Debug/MDd mismatch, -allow-unsupported-compiler, and the
  # FEMM_CUDA_CCBIN include-path-ordering workaround).
  set(BELASOLV_NVCC_ARGS -c "${CMAKE_CURRENT_SOURCE_DIR}/spars_cuda.cu" -o "${BELASOLV_CUDA_OBJ}")
  if(FEMM_CUDA_CCBIN)
    get_filename_component(BELASOLV_CUDA_CCBIN_TOOLSET "${FEMM_CUDA_CCBIN}/../../.." ABSOLUTE)
    list(APPEND BELASOLV_NVCC_ARGS -I "${BELASOLV_CUDA_CCBIN_TOOLSET}/include")
  endif()
  list(APPEND BELASOLV_NVCC_ARGS
      -I "${FEMM_CUDA_ROOT}/include"
      -allow-unsupported-compiler
      -Xcompiler "$<$<CONFIG:Debug>:/MDd>$<$<CONFIG:Release>:/MD>")
  foreach(_arch ${FEMM_CUDA_ARCHS})
    list(APPEND BELASOLV_NVCC_ARGS -gencode arch=compute_${_arch},code=sm_${_arch})
  endforeach()
  if(FEMM_CUDA_CCBIN)
    list(APPEND BELASOLV_NVCC_ARGS -ccbin "${FEMM_CUDA_CCBIN}")
  endif()

  add_custom_command(
    OUTPUT ${BELASOLV_CUDA_OBJ}
    COMMAND ${BELASOLV_NVCC} ${BELASOLV_NVCC_ARGS}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/spars_cuda.cu ${CMAKE_CURRENT_SOURCE_DIR}/spars_cuda.h
    COMMENT "Compiling belasolv's spars_cuda.cu with nvcc"
  )
  add_custom_target(belasolv_cuda_obj DEPENDS ${BELASOLV_CUDA_OBJ})
  set_source_files_properties(${BELASOLV_CUDA_OBJ} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
endif()

if(ENABLE_CUDA_SOLVER)
  add_executable(belasolv WIN32 ${belasolv_SOURCES} ${BELASOLV_CUDA_OBJ})
  add_dependencies(belasolv belasolv_cuda_obj)
  target_compile_definitions(belasolv PRIVATE FEMM_CUDA_ENABLED)
  target_include_directories(belasolv PRIVATE "${FEMM_CUDA_ROOT}/include")
  target_link_directories(belasolv PRIVATE "${FEMM_CUDA_ROOT}/lib/x64")
  target_link_libraries(belasolv cusparse cublas cudart delayimp)

  # Delay-load the CUDA runtime DLLs -- see fkn/CMakeLists.txt's identical
  # comment: a GPUAccel=0 run touches none of these DLLs at startup.
  file(GLOB BELASOLV_CUDA_DIRECT_IMPORT_DLLS
    "${FEMM_CUDA_ROOT}/bin/cudart64_*.dll"
    "${FEMM_CUDA_ROOT}/bin/cusparse64_*.dll"
    "${FEMM_CUDA_ROOT}/bin/cublas64_*.dll"
  )
  foreach(_dll ${BELASOLV_CUDA_DIRECT_IMPORT_DLLS})
    get_filename_component(_dllname "${_dll}" NAME)
    target_link_options(belasolv PRIVATE "/DELAYLOAD:${_dllname}")
  endforeach()
else()
  add_executable(belasolv WIN32 ${belasolv_SOURCES})
endif()
target_include_directories(belasolv PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS belasolv DESTINATION "${INSTALL_BIN_DIR}")

if(ENABLE_CUDA_SOLVER)
  # belasolv.exe dynamically links against these CUDA runtime DLLs; without
  # them next to the exe (or on PATH), it fails to even start -- see
  # fkn/CMakeLists.txt's identical comment.
  file(GLOB BELASOLV_CUDA_RUNTIME_DLLS
    "${FEMM_CUDA_ROOT}/bin/cudart64_*.dll"
    "${FEMM_CUDA_ROOT}/bin/cusparse64_*.dll"
    "${FEMM_CUDA_ROOT}/bin/cublas64_*.dll"
    "${FEMM_CUDA_ROOT}/bin/cublasLt64_*.dll"
    "${FEMM_CUDA_ROOT}/bin/nvJitLink_*.dll"
  )
  install(FILES ${BELASOLV_CUDA_RUNTIME_DLLS} DESTINATION "${INSTALL_BIN_DIR}")
  add_custom_command(TARGET belasolv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${BELASOLV_CUDA_RUNTIME_DLLS} "${INSTALL_BIN_DIR}"
    COMMENT "Copying CUDA runtime DLLs next to belasolv.exe"
  )
endif()