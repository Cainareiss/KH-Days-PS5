cmake_minimum_required(VERSION 3.23)
project(KHDaysPS5Core LANGUAGES CXX)

option(KHDAYS_PS5_BUILD_TESTS "Build the platform-neutral core smoke test" ON)
option(KHDAYS_PS5_NATIVE_BACKEND
       "Bind the PS5 adapter to platform.c audio_start/audio_stop" OFF)
option(KHDAYS_PS5_BUILD_RUNTIME
       "Build the Prospero ELF gameplay/runtime smoke target" OFF)

add_library(khdays-ps5-core STATIC
    core/src/frontend_core.cpp
    core/src/gameplay_core.cpp
    platform/ps5/audio_output.cpp
    platform/ps5/file_resource_provider.cpp
    platform/ps5/frontend_session.cpp
    platform/ps5/save_store.cpp
    platform/ps5/vulkan_videoout_adapter.cpp
)
target_compile_features(khdays-ps5-core PUBLIC cxx_std_20)
target_include_directories(khdays-ps5-core PUBLIC core/include ${CMAKE_CURRENT_SOURCE_DIR})
if(KHDAYS_PS5_NATIVE_BACKEND)
    target_compile_definitions(khdays-ps5-core PUBLIC KHDAYS_PS5_NATIVE_BACKEND=1)
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
