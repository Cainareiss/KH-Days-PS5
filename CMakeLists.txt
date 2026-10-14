cmake_minimum_required(VERSION 3.23)
project(KHDaysPS5Core LANGUAGES CXX)

option(KHDAYS_PS5_BUILD_TESTS "Build the platform-neutral core smoke test" ON)
option(KHDAYS_PS5_NATIVE_BACKEND
       "Bind the PS5 adapter to platform.c audio_start/audio_stop" OFF)
option(KHDAYS_PS5_BUILD_RUNTIME
       "Build the Prospero ELF gameplay/runtime smoke target" OFF)
option(KHDAYS_ENABLE_PC_ASSET_DECODERS
       "Build the neutral decoders from the local PC port" OFF)
set(KHDAYS_PC_PORT_ROOT "" CACHE PATH
    "Root of the KH 358/2 Days PC port containing include/ and src/assets/")

add_library(khdays-ps5-core STATIC
    core/src/frontend_core.cpp
    core/src/gameplay_core.cpp
    core/src/world_runtime.cpp
    platform/ps5/audio_output.cpp
    platform/ps5/file_resource_provider.cpp
    platform/ps5/frontend_session.cpp
    platform/ps5/save_store.cpp
    platform/ps5/vulkan_videoout_adapter.cpp
    platform/ps5/world_session.cpp
)
target_compile_features(khdays-ps5-core PUBLIC cxx_std_20)
target_include_directories(khdays-ps5-core PUBLIC core/include ${CMAKE_CURRENT_SOURCE_DIR})
if(KHDAYS_PS5_NATIVE_BACKEND)
    target_compile_definitions(khdays-ps5-core PUBLIC KHDAYS_PS5_NATIVE_BACKEND=1)
endif()

if(KHDAYS_ENABLE_PC_ASSET_DECODERS)
    if(NOT KHDAYS_PC_PORT_ROOT OR
       NOT EXISTS "${KHDAYS_PC_PORT_ROOT}/include/khdays/assets/message.h")
        message(FATAL_ERROR
            "KHDAYS_PC_PORT_ROOT must point to the local PC port source tree")
    endif()

    set(KHDAYS_MOBICLIP_REFERENCE
        "${KHDAYS_PC_PORT_ROOT}/external/khdays-decomp/libs/mobiclip/video/portable/mobiclip_reference.cpp")
    if(NOT EXISTS "${KHDAYS_MOBICLIP_REFERENCE}")
        message(FATAL_ERROR "MobiClip reference implementation not found")
    endif()

    add_library(khdays-pc-asset-decoders STATIC
        "${KHDAYS_PC_PORT_ROOT}/src/assets/animation.cpp"
        "${KHDAYS_PC_PORT_ROOT}/src/assets/audio.cpp"
        "${KHDAYS_PC_PORT_ROOT}/src/assets/cakp.cpp"
        "${KHDAYS_PC_PORT_ROOT}/src/assets/cell.cpp"
        "${KHDAYS_PC_PORT_ROOT}/src/assets/font.cpp"
        "${KHDAYS_PC_PORT_ROOT}/src/assets/graphics2d.cpp"
        "${KHDAYS_PC_PORT_ROOT}/src/assets/mdl0.cpp"
        "${KHDAYS_PC_PORT_ROOT}/src/assets/mesh.cpp"
        "${KHDAYS_PC_PORT_ROOT}/src/assets/message.cpp"
        "${KHDAYS_PC_PORT_ROOT}/src/assets/mobiclip.cpp"
        "${KHDAYS_PC_PORT_ROOT}/src/assets/mods.cpp"
        "${KHDAYS_PC_PORT_ROOT}/src/assets/mods_video.cpp"
        "${KHDAYS_PC_PORT_ROOT}/src/assets/sdat.cpp"
        "${KHDAYS_PC_PORT_ROOT}/src/assets/tex0.cpp"
        "${KHDAYS_MOBICLIP_REFERENCE}")
    target_compile_features(khdays-pc-asset-decoders PUBLIC cxx_std_20)
    target_include_directories(khdays-pc-asset-decoders PUBLIC
        "${KHDAYS_PC_PORT_ROOT}/include"
        "${KHDAYS_PC_PORT_ROOT}/external/khdays-decomp/libs/mobiclip/video/portable")

    add_library(khdays-ps5-asset-bridge STATIC
        platform/ps5/asset_decode_bridge.cpp)
    target_compile_features(khdays-ps5-asset-bridge PUBLIC cxx_std_20)
    target_include_directories(khdays-ps5-asset-bridge PUBLIC platform/ps5)
    target_link_libraries(khdays-ps5-asset-bridge PUBLIC khdays-pc-asset-decoders)

    add_executable(khdays-ps5-asset-smoke platform/ps5/asset_decode_smoke.cpp)
    target_link_libraries(khdays-ps5-asset-smoke PRIVATE khdays-ps5-asset-bridge)
endif()

if(KHDAYS_PS5_BUILD_RUNTIME)
    add_executable(khdays-ps5-runtime platform/ps5/ps5_runtime_main.cpp)
    target_link_libraries(khdays-ps5-runtime PRIVATE khdays-ps5-core)
    # v0.44's Windows wrapper already contributes crt1.o; CMake's default
    # FreeBSD link also adds it, so suppress the duplicate startup object.
    target_link_options(khdays-ps5-runtime PRIVATE
        -nostartfiles -Wl,-no-pie "${PS5_PAYLOAD_SDK}/target/lib/crt1.o")
    set_target_properties(khdays-ps5-runtime PROPERTIES
        OUTPUT_NAME "khdays-days-runtime.elf")
endif()

if(KHDAYS_PS5_BUILD_TESTS)
    enable_testing()
    add_executable(khdays-ps5-core-smoke tests/frontend_core_smoke.cpp)
    target_link_libraries(khdays-ps5-core-smoke PRIVATE khdays-ps5-core)
    add_test(NAME frontend_core_smoke COMMAND khdays-ps5-core-smoke)
endif()
