cmake_minimum_required(VERSION 3.8.0)
project (Wavix C CXX ASM)

find_package(WAVM REQUIRED CONFIG)
find_package(LLVM REQUIRED CONFIG)

add_executable(wavix file.cpp file.h memory.cpp process.cpp wavix.cpp errno.h process.h wavix.h)
target_link_libraries(wavix WAVM::Logging WAVM::IR WAVM::WASM WAVM::Runtime)
install(TARGETS wavix DESTINATION bin)

# This adds the WAVM RPATH to the installed binary.
set_target_properties(wavix PROPERTIES INSTALL_RPATH_USE_LINK_PATH true)

if(NOT MSVC)
	# Disable zero-length printf format string warning.
	target_compile_options(wavix PRIVATE "-Wno-format-zero-length")
endif()