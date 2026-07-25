cmake_minimum_required(VERSION 3.10)
project(hsolv C CXX)

set(INSTALL_BIN_DIR  "${CMAKE_SOURCE_DIR}" CACHE PATH "Path where exe will be installed")

find_package(MFC REQUIRED)
set(CMAKE_MFC_FLAG 2)
add_definitions(-D_WIN32_WINNT=0x0501)

set(hsolv_SOURCES
  complex.h
  HSOLV.H
  hsolvDlg.h
  hsolvdoc.h
  MESH.H
  RESOURCE.H
  SPARS.H
  spars_cuda.h
  StdAfx.h
  complex.cpp
  CUTHILL.CPP
  hsolv.cpp
  hsolvDlg.cpp
  hsolvdoc.cpp
  MAIN.CPP
  prob1big.cpp
  SPARS.CPP
  STDAFX.CPP
  hsolv.rc
)

# Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
# optional CUDA-accelerated linear solve (spars_cuda.cu/.h), mirroring
# fkn/CMakeLists.txt's identical block for the magnetics solver.
# ENABLE_CUDA_SOLVER is declared once, at the root CMakeLists.txt (before
# add_subdirectory(hsolv)), so it doesn't need to be redeclared here.
if(ENABLE_CUDA_SOLVER)
  # Same cache variables fkn/CMakeLists.txt declares -- CMake cache vars
  # are global, so whichever of fkn/hsolv's add_subdirectory() runs first
  # actually sets these, but each declares them independently so neither
  # depends on the other's subdirectory ordering.
  set(FEMM_CUDA_ROOT "" CACHE PATH "CUDA Toolkit root, e.g. \"C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6\"")
  set(FEMM_CUDA_CCBIN "" CACHE PATH "Optional nvcc -ccbin override: an alternate MSVC toolset bin dir, needed if the default one is newer than the CUDA Toolkit supports")
  set(FEMM_CUDA_ARCHS "60;75;86;89" CACHE STRING "CUDA compute capabilities to build for (semicolon-separated, no dots)")
  if(NOT FEMM_CUDA_ROOT)
    message(FATAL_ERROR "ENABLE_CUDA_SOLVER requires -DFEMM_CUDA_ROOT=<path to CUDA Toolkit>")
  endif()

  set(HSOLV_NVCC "${FEMM_CUDA_ROOT}/bin/nvcc.exe")
  set(HSOLV_CUDA_OBJ "${CMAKE_CURRENT_BINARY_DIR}/spars_cuda.obj")

  # Match the MSVC runtime library the rest of the (MFC) project is built
  # with -- see fkn/CMakeLists.txt's identical comment for the full
  # rationale (Debug/MDd mismatch, -allow-unsupported-compiler, and the
  # FEMM_CUDA_CCBIN include-path-ordering workaround).
  set(HSOLV_NVCC_ARGS -c "${CMAKE_CURRENT_SOURCE_DIR}/spars_cuda.cu" -o "${HSOLV_CUDA_OBJ}")
  if(FEMM_CUDA_CCBIN)
    get_filename_component(HSOLV_CUDA_CCBIN_TOOLSET "${FEMM_CUDA_CCBIN}/../../.." ABSOLUTE)
    list(APPEND HSOLV_NVCC_ARGS -I "${HSOLV_CUDA_CCBIN_TOOLSET}/include")
  endif()
  list(APPEND HSOLV_NVCC_ARGS
      -I "${FEMM_CUDA_ROOT}/include"
      -allow-unsupported-compiler
      -Xcompiler "$<$<CONFIG:Debug>:/MDd>$<$<CONFIG:Release>:/MD>")
  foreach(_arch ${FEMM_CUDA_ARCHS})
    list(APPEND HSOLV_NVCC_ARGS -gencode arch=compute_${_arch},code=sm_${_arch})
  endforeach()
  if(FEMM_CUDA_CCBIN)
    list(APPEND HSOLV_NVCC_ARGS -ccbin "${FEMM_CUDA_CCBIN}")
  endif()

  add_custom_command(
    OUTPUT ${HSOLV_CUDA_OBJ}
    COMMAND ${HSOLV_NVCC} ${HSOLV_NVCC_ARGS}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/spars_cuda.cu ${CMAKE_CURRENT_SOURCE_DIR}/spars_cuda.h
    COMMENT "Compiling hsolv's spars_cuda.cu with nvcc"
  )
  add_custom_target(hsolv_cuda_obj DEPENDS ${HSOLV_CUDA_OBJ})
  set_source_files_properties(${HSOLV_CUDA_OBJ} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
endif()

if(ENABLE_CUDA_SOLVER)
  add_executable(hsolv WIN32 ${hsolv_SOURCES} ${HSOLV_CUDA_OBJ})
  add_dependencies(hsolv hsolv_cuda_obj)
  target_compile_definitions(hsolv PRIVATE FEMM_CUDA_ENABLED)
  target_include_directories(hsolv PRIVATE "${FEMM_CUDA_ROOT}/include")
  target_link_directories(hsolv PRIVATE "${FEMM_CUDA_ROOT}/lib/x64")
  target_link_libraries(hsolv cusparse cublas cudart delayimp)

  # Delay-load the CUDA runtime DLLs -- see fkn/CMakeLists.txt's identical
  # comment: a GPUAccel=0 run touches none of these DLLs at startup.
  file(GLOB HSOLV_CUDA_DIRECT_IMPORT_DLLS
    "${FEMM_CUDA_ROOT}/bin/cudart64_*.dll"
    "${FEMM_CUDA_ROOT}/bin/cusparse64_*.dll"
    "${FEMM_CUDA_ROOT}/bin/cublas64_*.dll"
  )
  foreach(_dll ${HSOLV_CUDA_DIRECT_IMPORT_DLLS})
    get_filename_component(_dllname "${_dll}" NAME)
    target_link_options(hsolv PRIVATE "/DELAYLOAD:${_dllname}")
  endforeach()
else()
  add_executable(hsolv WIN32 ${hsolv_SOURCES})
endif()
target_include_directories(hsolv PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS hsolv DESTINATION "${INSTALL_BIN_DIR}")

if(ENABLE_CUDA_SOLVER)
  # hsolv.exe dynamically links against these CUDA runtime DLLs; without
  # them next to the exe (or on PATH), it fails to even start -- see
  # fkn/CMakeLists.txt's identical comment.
  file(GLOB HSOLV_CUDA_RUNTIME_DLLS
    "${FEMM_CUDA_ROOT}/bin/cudart64_*.dll"
    "${FEMM_CUDA_ROOT}/bin/cusparse64_*.dll"
    "${FEMM_CUDA_ROOT}/bin/cublas64_*.dll"
    "${FEMM_CUDA_ROOT}/bin/cublasLt64_*.dll"
    "${FEMM_CUDA_ROOT}/bin/nvJitLink_*.dll"
  )
  install(FILES ${HSOLV_CUDA_RUNTIME_DLLS} DESTINATION "${INSTALL_BIN_DIR}")
  add_custom_command(TARGET hsolv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${HSOLV_CUDA_RUNTIME_DLLS} "${INSTALL_BIN_DIR}"
    COMMENT "Copying CUDA runtime DLLs next to hsolv.exe"
  )
endif()
