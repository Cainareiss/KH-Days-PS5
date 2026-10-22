# The generated tree preserves relative includes and leaves the upstream cache
# untouched. Set KHDAYS_NATIVE_RUNTIME_PORT to prepare_native_runtime_sources.py's
# --output. Host video/audio/input services belong to the PS5 platform target.
if(NOT EXISTS "${KHDAYS_NATIVE_RUNTIME_PORT}/sources.cmake")
    message(FATAL_ERROR "Prepare the independent native runtime tree first")
endif()
include("${KHDAYS_NATIVE_RUNTIME_PORT}/sources.cmake")
list(APPEND KHDAYS_NATIVE_PS5_RUNTIME_C_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/ps5/runtime_posix.c"
    "${CMAKE_CURRENT_LIST_DIR}/ps5/diag_ps5.c"
    "${CMAKE_CURRENT_LIST_DIR}/ps5/io_guard_ps5.c")
set(KHDAYS_NATIVE_PS5_RUNTIME_INCLUDE_DIRS
    "${KHDAYS_NATIVE_RUNTIME_PORT}"
    "${KHDAYS_NATIVE_RUNTIME_PORT}/runtime/core"
    "${CMAKE_CURRENT_LIST_DIR}/ps5")
# C++/VFS units are compiled by runtime_assets.cmake. The generated
# adaptation.json inventories their sources, hashes and native replacements.
