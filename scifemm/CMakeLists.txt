cmake_minimum_required(VERSION 3.10)
project(scifemm C CXX)

find_package(MFC REQUIRED)
set(CMAKE_MFC_FLAG 2)
add_definitions(-D_WIN32_WINNT=0x0501)

set(scifemm_SOURCES
  femm.h
  femm.cpp
  scilink.cpp
)

# Built as a SHARED library (was a static lib, which nothing else linked
# against -- see the source's existing __declspec(dllexport) on openfemm/
# call2femm/closefemm in scilink.cpp, unused until now) named scilink.dll
# to match what scifemm.sci actually loads via link(scifemm_path +
# "scilink.dll", ...). Installed next to scifemm.sci itself (not
# INSTALL_BIN_DIR) since scifemm_path is derived from scifemm.sci's own
# location at Scilab runtime.
add_library(scifemm SHARED ${scifemm_SOURCES})
target_include_directories(scifemm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(scifemm PROPERTIES OUTPUT_NAME "scilink")
install(TARGETS scifemm RUNTIME DESTINATION "${CMAKE_SOURCE_DIR}/scifemm")
