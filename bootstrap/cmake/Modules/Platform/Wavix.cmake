cmake_minimum_required(VERSION 3.4.0)

set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR wasm32)
set(triple wasm32-unknown-wavix)

set(UNIX True)

set(CMAKE_SYSROOT ${WAVIX_SYS_DIR})
set(CMAKE_STAGING_PREFIX ${WAVIX_SYS_DIR})

set(CMAKE_C_COMPILER_TARGET ${triple})
set(CMAKE_CXX_COMPILER_TARGET ${triple})

# Don't look in the sysroot for executables to run during the build
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Only look in the sysroot (not in the host paths) for the rest
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)