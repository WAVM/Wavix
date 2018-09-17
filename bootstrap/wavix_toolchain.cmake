# Cmake toolchain description file for the clang wasm toolchain

cmake_minimum_required(VERSION 3.4.0)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/Modules")

set(WAVIX_HOST_DIR ${CMAKE_CURRENT_LIST_DIR})
set(WAVIX_SYS_DIR ${CMAKE_CURRENT_LIST_DIR}/../sys)

# This has to go before we set CMAKE_SYSTEM_NAME because the default c compiler
# gets set before the platform file is included
if (CMAKE_HOST_WIN32)
  set(EXE_SUFFIX ".exe")
else()
  set(EXE_SUFFIX "")
endif()

if ("${CMAKE_C_COMPILER}" STREQUAL "")
  set(CMAKE_C_COMPILER ${WAVIX_HOST_DIR}/bin/clang${EXE_SUFFIX})
endif()
if ("${CMAKE_CXX_COMPILER}" STREQUAL "")
  set(CMAKE_CXX_COMPILER ${WAVIX_HOST_DIR}/bin/clang++${EXE_SUFFIX})
endif()
if ("${CMAKE_AR}" STREQUAL "")
  set(CMAKE_AR ${WAVIX_HOST_DIR}/bin/llvm-ar${EXE_SUFFIX} CACHE FILEPATH "llvm ar")
endif()
if ("${CMAKE_RANLIB}" STREQUAL "")
 set(CMAKE_RANLIB ${WAVIX_HOST_DIR}/bin/llvm-ranlib${EXE_SUFFIX} CACHE FILEPATH "llvm ranlib")
endif()

set(CMAKE_SYSTEM_NAME Wavix)
