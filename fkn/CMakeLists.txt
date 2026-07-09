cmake_minimum_required(VERSION 3.10)
project(fkn C CXX)

set(INSTALL_BIN_DIR  "${CMAKE_SOURCE_DIR}" CACHE PATH "Path where exe will be installed")

find_package(MFC REQUIRED)
set(CMAKE_MFC_FLAG 2)
add_definitions(-D_WIN32_WINNT=0x0501)

set(fkn_SOURCES
  complex.h
  femmedoccore.h
  fkn.h
  fknDlg.h
  fullmatrix.h
  #lua.h
  mesh.h
  resource.h
  spars.h
  spars_cuda.h
  StdAfx.h
  #complex.cpp
  cspars.cpp
  cuthill.cpp
  femmedoccore.cpp
  fkn.cpp
  fknDlg.cpp
  fullmatrix.cpp
  main.cpp
  matprop.cpp
  prob1big.cpp
  prob2big.cpp
  prob3big.cpp
  prob4big.cpp
  spars.cpp
  StdAfx.cpp
  #tmp_prob1big.cpp
  fkn.rc
)

# Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-08:
# optional CUDA-accelerated linear solve (spars_cuda.cu/.h). Off by
# default -- when OFF (the normal CI/build.ps1 path), fkn builds exactly
# as before this option existed. Needs a machine with the CUDA Toolkit
# and an NVIDIA GPU to build/use; not required for a normal FEMM build.
option(ENABLE_CUDA_SOLVER "Build fkn with an optional CUDA-accelerated linear solver" OFF)

if(ENABLE_CUDA_SOLVER)
  set(FEMM_CUDA_ROOT "" CACHE PATH "CUDA Toolkit root, e.g. \"C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6\"")
  set(FEMM_CUDA_CCBIN "" CACHE PATH "Optional nvcc -ccbin override: an alternate MSVC toolset bin dir, needed if the default one is newer than the CUDA Toolkit supports")
  set(FEMM_CUDA_ARCHS "60;75;86;89" CACHE STRING "CUDA compute capabilities to build for (semicolon-separated, no dots)")
  if(NOT FEMM_CUDA_ROOT)
    message(FATAL_ERROR "ENABLE_CUDA_SOLVER requires -DFEMM_CUDA_ROOT=<path to CUDA Toolkit>")
  endif()

  set(FEMM_NVCC "${FEMM_CUDA_ROOT}/bin/nvcc.exe")
  set(FEMM_CUDA_OBJ "${CMAKE_CURRENT_BINARY_DIR}/spars_cuda.obj")

  # Match the MSVC runtime library the rest of the (MFC) project is built
  # with -- fkn (like the rest of this codebase) always builds as Debug,
  # which uses /MDd; without this, linking fails with LNK2038 CRT/iterator-
  # debug-level mismatches between spars_cuda.obj and the other objects.
  set(FEMM_NVCC_ARGS -c "${CMAKE_CURRENT_SOURCE_DIR}/spars_cuda.cu" -o "${FEMM_CUDA_OBJ}"
      -I "${FEMM_CUDA_ROOT}/include"
      -Xcompiler "$<$<CONFIG:Debug>:/MDd>$<$<CONFIG:Release>:/MD>")
  foreach(_arch ${FEMM_CUDA_ARCHS})
    list(APPEND FEMM_NVCC_ARGS -gencode arch=compute_${_arch},code=sm_${_arch})
  endforeach()
  if(FEMM_CUDA_CCBIN)
    list(APPEND FEMM_NVCC_ARGS -ccbin "${FEMM_CUDA_CCBIN}")
  endif()

  add_custom_command(
    OUTPUT ${FEMM_CUDA_OBJ}
    COMMAND ${FEMM_NVCC} ${FEMM_NVCC_ARGS}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/spars_cuda.cu ${CMAKE_CURRENT_SOURCE_DIR}/spars_cuda.h
    COMMENT "Compiling spars_cuda.cu with nvcc"
  )
  add_custom_target(fkn_cuda_obj DEPENDS ${FEMM_CUDA_OBJ})
  set_source_files_properties(${FEMM_CUDA_OBJ} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
endif()

if(ENABLE_CUDA_SOLVER)
  add_executable(fkn WIN32 ${fkn_SOURCES} ${FEMM_CUDA_OBJ})
  add_dependencies(fkn fkn_cuda_obj)
  target_compile_definitions(fkn PRIVATE FEMM_CUDA_ENABLED)
  target_include_directories(fkn PRIVATE "${FEMM_CUDA_ROOT}/include")
  target_link_directories(fkn PRIVATE "${FEMM_CUDA_ROOT}/lib/x64")
  target_link_libraries(fkn cusparse cublas cudart)
else()
  add_executable(fkn WIN32 ${fkn_SOURCES})
endif()
target_include_directories(fkn PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fkn liblua)
install(TARGETS fkn DESTINATION "${INSTALL_BIN_DIR}")

if(ENABLE_CUDA_SOLVER)
  # fkn.exe dynamically links against these CUDA runtime DLLs (cusparse
  # also pulls in nvJitLink transitively at run time); without them next
  # to the exe (or on PATH), it fails to even start -- silently, with no
  # error message of its own, since the OS loader rejects it before
  # main() runs. Bundling them here means a built femmx doesn't require
  # end users to have the CUDA Toolkit's bin dir on their system PATH.
  file(GLOB FEMM_CUDA_RUNTIME_DLLS
    "${FEMM_CUDA_ROOT}/bin/cudart64_*.dll"
    "${FEMM_CUDA_ROOT}/bin/cusparse64_*.dll"
    "${FEMM_CUDA_ROOT}/bin/cublas64_*.dll"
    "${FEMM_CUDA_ROOT}/bin/cublasLt64_*.dll"
    "${FEMM_CUDA_ROOT}/bin/nvJitLink_*.dll"
  )
  install(FILES ${FEMM_CUDA_RUNTIME_DLLS} DESTINATION "${INSTALL_BIN_DIR}")
  add_custom_command(TARGET fkn POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FEMM_CUDA_RUNTIME_DLLS} "${INSTALL_BIN_DIR}"
    COMMENT "Copying CUDA runtime DLLs next to fkn.exe"
  )
endif()
