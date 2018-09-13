macro(configure_out_of_tree_llvm)
  message(STATUS "Configuring for standalone build.")
  set(LIBCXXABI_STANDALONE_BUILD 1)

  # Add LLVM Functions --------------------------------------------------------
  if (WIN32)
    set(LLVM_ON_UNIX 0)
    set(LLVM_ON_WIN32 1)
  else()
    set(LLVM_ON_UNIX 1)
    set(LLVM_ON_WIN32 0)
  endif()

  # LLVM Options --------------------------------------------------------------
  set(LLVM_FOUND 0)
  set(LLVM_INCLUDE_TESTS ${LLVM_FOUND})
  set(LLVM_INCLUDE_DOCS ${LLVM_FOUND})
  set(LLVM_ENABLE_SPHINX OFF)

  # In a standalone build, we don't have llvm to automatically generate the
  # llvm-lit script for us.  So we need to provide an explicit directory that
  # the configurator should write the script into.
  set(LLVM_LIT_OUTPUT_DIR "${libcxxabi_BINARY_DIR}/bin")

  if (LLVM_INCLUDE_TESTS)
    # Required LIT Configuration ------------------------------------------------
    # Define the default arguments to use with 'lit', and an option for the user
    # to override.
    set(LLVM_EXTERNAL_LIT "${LLVM_MAIN_SRC_DIR}/utils/lit/lit.py")
    set(LIT_ARGS_DEFAULT "-sv --show-xfail --show-unsupported")
    if (MSVC OR XCODE)
      set(LIT_ARGS_DEFAULT "${LIT_ARGS_DEFAULT} --no-progress-bar")
    endif()
    set(LLVM_LIT_ARGS "${LIT_ARGS_DEFAULT}" CACHE STRING "Default options for lit")
  endif()

  # Required doc configuration
  if (LLVM_ENABLE_SPHINX)
    find_package(Sphinx REQUIRED)
  endif()

  if (LLVM_ON_UNIX AND NOT APPLE)
    set(LLVM_HAVE_LINK_VERSION_SCRIPT 1)
  else()
    set(LLVM_HAVE_LINK_VERSION_SCRIPT 0)
  endif()
endmacro(configure_out_of_tree_llvm)

configure_out_of_tree_llvm()
