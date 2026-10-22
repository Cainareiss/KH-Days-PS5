cmake_minimum_required(VERSION 3.23)
project(KHDaysNativePS5Platform LANGUAGES C ASM)

set(KHDAYS_NATIVE_UPSTREAM_ROOT "" CACHE PATH
    "Pinned khdays-port source tree used by the PS5 platform layer")

if(NOT EXISTS "${KHDAYS_NATIVE_UPSTREAM_ROOT}/port/host/app/context.h")
    message(FATAL_ERROR
        "KHDAYS_NATIVE_UPSTREAM_ROOT must point to a khdays-port v0.1.1 tree")
endif()
# Upstream reference renderers, geometry and the complete native sound engine.
# These sources are independent of SDL and use the same fixed third-party
# revisions as the pinned upstream build.
option(KHDAYS_NATIVE_BUILD_MEDIA "Compile upstream graphics and sound" ON)
if(KHDAYS_NATIVE_BUILD_MEDIA)
    include(FetchContent)
    FetchContent_Declare(khdays_stb
        URL https://codeload.github.com/nothings/stb/tar.gz/2c980bb59875b0d32144a71867fbdebb2f77cd20
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR no-configure-ps5)
    FetchContent_Declare(khdays_dr_libs
        URL https://codeload.github.com/mackron/dr_libs/tar.gz/dfe8377631000664666519fdb83da193fd8037f4
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR no-configure-ps5)
    FetchContent_MakeAvailable(khdays_stb khdays_dr_libs)
    set(port "${KHDAYS_NATIVE_UPSTREAM_ROOT}/port")
    add_library(khdays-native-ps5-gpu STATIC
        "${port}/gpu/gpu2d.c" "${port}/gpu/gpu3d.c"
        "${port}/gpu/texdecode.c" "${port}/gpu/vram_map.c")
    add_library(khdays-native-ps5-gfx STATIC
        "${port}/gfx/describe.c"
        "${port}/gfx/g3d_geometry.c" "${port}/gfx/g3d_interp.c")
    if(KHDAYS_NATIVE_BUILD_RUNTIME)
        # Prepared later in this configure pass; same pixel arithmetic, with
        # explicit per-call context instead of emulated TLS in the hot loop.
        target_sources(khdays-native-ps5-gfx PRIVATE "${CMAKE_BINARY_DIR}/runtime-sources/gfx/compose_cpu.c")
    else()
        target_sources(khdays-native-ps5-gfx PRIVATE "${port}/gfx/compose_cpu.c")
    endif()
    set(sound_sources archive bus categories channels mix_ds mix_pc render replace
        snd7_alarm snd7_bank snd7_channel snd7_command snd7_driver snd7_hw snd7_os
        snd7_seq snd7_tables stream_reader third_party_decoders)
    set(sound_paths "")
    foreach(source IN LISTS sound_sources)
        list(APPEND sound_paths "${port}/sound/${source}.c")
    endforeach()
    add_library(khdays-native-ps5-sound STATIC ${sound_paths})
    target_include_directories(khdays-native-ps5-sound PRIVATE
        "${khdays_stb_SOURCE_DIR}" "${khdays_dr_libs_SOURCE_DIR}")
    # MSVC's stdint.h supplied NULL transitively; the FreeBSD header does not.
    target_compile_options(khdays-native-ps5-sound PRIVATE -fdeclspec -include stddef.h)
    foreach(target khdays-native-ps5-gpu khdays-native-ps5-gfx khdays-native-ps5-sound)
        target_compile_features(${target} PRIVATE c_std_17)
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endforeach()
endif()

add_library(khdays-native-ps5-context STATIC
    ps5/context_ps5.c
    ps5/context_ps5_x64.S)
target_compile_features(khdays-native-ps5-context PRIVATE c_std_17)
target_include_directories(khdays-native-ps5-context PUBLIC
    "${KHDAYS_NATIVE_UPSTREAM_ROOT}/port/host/app")
target_compile_options(khdays-native-ps5-context PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Werror>)

# Compile the upstream thread contract against the PS5 header and ABI.  This
# remains an object target until a hardware runner can report its result.
add_library(khdays-native-ps5-thread-contract OBJECT
    "${KHDAYS_NATIVE_UPSTREAM_ROOT}/port/tests/thread_test.c")
target_compile_features(khdays-native-ps5-thread-contract PRIVATE c_std_17)
target_include_directories(khdays-native-ps5-thread-contract PRIVATE
    "${KHDAYS_NATIVE_UPSTREAM_ROOT}/port/host/app")
target_compile_options(khdays-native-ps5-thread-contract PRIVATE
    -Wall -Wextra -Werror)

add_library(khdays-native-ps5-host-contract STATIC
    ps5/files_ps5.c
    ps5/clock_ps5.c)
target_compile_features(khdays-native-ps5-host-contract PRIVATE c_std_17)
target_include_directories(khdays-native-ps5-host-contract PUBLIC
    "${KHDAYS_NATIVE_UPSTREAM_ROOT}/port/host/app"
    "${KHDAYS_NATIVE_UPSTREAM_ROOT}/port/runtime/core"
    "${KHDAYS_NATIVE_UPSTREAM_ROOT}/port/hal")
target_compile_options(khdays-native-ps5-host-contract PRIVATE
    -Wall -Wextra -Werror)

# The full game's function references and initializers must survive ELF
# section GC. COFF's $a/$m/$z sentinel ordering does not apply on PS5.
add_library(khdays-native-ps5-game-tables STATIC
    ps5/data_init_ps5.c
    ps5/window_ps5.c)
target_compile_features(khdays-native-ps5-game-tables PRIVATE c_std_17)
target_include_directories(khdays-native-ps5-game-tables PUBLIC
    "${KHDAYS_NATIVE_UPSTREAM_ROOT}/port/runtime/core")
target_compile_options(khdays-native-ps5-game-tables PRIVATE
    -Wall -Wextra -Werror)
target_link_options(khdays-native-ps5-game-tables INTERFACE
    "LINKER:-T,${CMAKE_CURRENT_SOURCE_DIR}/ps5/game_tables.ld")

set(KHDAYS_NATIVE_ELF_SOURCES "" CACHE PATH
    "ELF-adapted output from prepare_native_elf_sources.py")
if(KHDAYS_NATIVE_ELF_SOURCES)
    if(NOT EXISTS "${KHDAYS_NATIVE_ELF_SOURCES}/adaptation.json")
        message(FATAL_ERROR "Run prepare_native_elf_sources.py before compiling the game")
    endif()
    include("${KHDAYS_NATIVE_ELF_SOURCES}/sources.cmake")
    add_library(khdays-native-ps5-game STATIC ${KHDAYS_REWRITTEN})
    target_compile_features(khdays-native-ps5-game PRIVATE c_std_17)
    target_include_directories(khdays-native-ps5-game PRIVATE
        "${KHDAYS_NATIVE_UPSTREAM_ROOT}/port/compat"
        "${KHDAYS_NATIVE_ELF_SOURCES}")
    target_compile_options(khdays-native-ps5-game PRIVATE
        -fpack-struct=4 -fsigned-char -fno-strict-aliasing -fwrapv
        -Wno-incompatible-pointer-types -Wno-int-conversion
        -Wno-unused-value -Wno-unknown-pragmas)
endif()

set(KHDAYS_NATIVE_GENERATED_ROOT "" CACHE PATH
    "prepare.py output containing khdays_data_area.h")
if(KHDAYS_NATIVE_GENERATED_ROOT)
    if(NOT EXISTS "${KHDAYS_NATIVE_GENERATED_ROOT}/khdays_data_area.h")
        message(FATAL_ERROR
            "KHDAYS_NATIVE_GENERATED_ROOT does not contain khdays_data_area.h")
    endif()
    add_library(khdays-native-ps5-memory-contract OBJECT ps5/memory_ps5.c)
    target_compile_features(khdays-native-ps5-memory-contract PRIVATE c_std_17)
    target_include_directories(khdays-native-ps5-memory-contract PRIVATE
        "${KHDAYS_NATIVE_UPSTREAM_ROOT}/port/host/app"
        "${KHDAYS_NATIVE_UPSTREAM_ROOT}/port/runtime/core"
        "${KHDAYS_NATIVE_UPSTREAM_ROOT}/port/hal"
        "${KHDAYS_NATIVE_UPSTREAM_ROOT}/port/sound"
        "${KHDAYS_NATIVE_GENERATED_ROOT}")
    target_compile_options(khdays-native-ps5-memory-contract PRIVATE
        -Wall -Wextra -Werror)
endif()

# The option is off for isolated context/media contract builds.
option(KHDAYS_NATIVE_BUILD_RUNTIME "Integrate the native PS5 runtime services" OFF)
if(KHDAYS_NATIVE_BUILD_RUNTIME)
    if(NOT KHDAYS_NATIVE_BUILD_MEDIA OR NOT KHDAYS_NATIVE_GENERATED_ROOT)
        message(FATAL_ERROR "Runtime integration needs media and generated game headers")
    endif()
    include("${CMAKE_CURRENT_SOURCE_DIR}/runtime_integration.cmake")
endif()

# Link the validated runtime archive into a real PS5 ELF.  The Windows LLVM
# driver cannot select the PS5 linker for this target, so the reproducible
# link is delegated to link_native_ps5_elf.ps1, which invokes ld.lld directly.
option(KHDAYS_NATIVE_BUILD_ELF "Link the native runtime into a PS5 ELF" OFF)
if(KHDAYS_NATIVE_BUILD_ELF)
    if(NOT TARGET khdays-native-ps5-runtime)
        message(FATAL_ERROR "KHDAYS_NATIVE_BUILD_ELF requires KHDAYS_NATIVE_BUILD_RUNTIME")
    endif()
    add_library(khdays-native-ps5-entry OBJECT ps5/main_ps5.c)
    target_include_directories(khdays-native-ps5-entry PRIVATE
        "${KHDAYS_NATIVE_UPSTREAM_ROOT}/port/host/app"
        "${CMAKE_CURRENT_LIST_DIR}/ps5")
    find_program(KHDAYS_POWERSHELL powershell.exe REQUIRED)
    add_custom_target(khdays-native-ps5-elf
        COMMAND "${KHDAYS_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_LIST_DIR}/../scripts/link_native_ps5_elf.ps1"
            -BuildDir "${CMAKE_BINARY_DIR}"
            -MainObject "$<TARGET_OBJECTS:khdays-native-ps5-entry>"
        DEPENDS khdays-native-ps5-runtime khdays-native-ps5-entry
        BYPRODUCTS "${CMAKE_BINARY_DIR}/khdays-days-runtime.elf"
        VERBATIM)
endif()

