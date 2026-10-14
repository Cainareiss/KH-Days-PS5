cmake_minimum_required(VERSION 3.23)
project(KHDaysPS5Core LANGUAGES CXX)

option(KHDAYS_PS5_BUILD_TESTS "Build the platform-neutral core smoke test" ON)
option(KHDAYS_PS5_NATIVE_BACKEND
       "Bind the PS5 adapter to platform.c audio_start/audio_stop" OFF)
option(KHDAYS_PS5_BUILD_RUNTIME
       "Build the Prospero ELF gameplay/runtime smoke target" OFF)
option(KHDAYS_ENABLE_PC_ASSET_DECODERS
       "Build the neutral decoders from the local PC port" OFF)
option(KHDAYS_PS5_BUILD_VULKAN_MEDIA_BACKEND
       "Build the concrete PS5 Vulkan media resource backend" OFF)
set(KHDAYS_PC_PORT_ROOT "" CACHE PATH
    "Root of the KH 358/2 Days PC port containing include/ and src/assets/")
set(KHDAYS_PS5_VULKAN_TEMPLATE_ROOT "" CACHE PATH
    "Root of PS5_VulkanTemplate containing base/, external/ and ps5/")

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

if(KHDAYS_PS5_BUILD_VULKAN_MEDIA_BACKEND)
    if(NOT KHDAYS_PS5_VULKAN_TEMPLATE_ROOT OR
       NOT EXISTS "${KHDAYS_PS5_VULKAN_TEMPLATE_ROOT}/base/VulkanDevice.h" OR
       NOT EXISTS "${KHDAYS_PS5_VULKAN_TEMPLATE_ROOT}/ps5/third_party/volk/volk.h")
        message(FATAL_ERROR
            "KHDAYS_PS5_VULKAN_TEMPLATE_ROOT must point to PS5_VulkanTemplate")
    endif()
    add_library(khdays-ps5-vulkan-media-backend STATIC
        platform/ps5/vulkan_media_backend.cpp)
    target_compile_features(khdays-ps5-vulkan-media-backend PUBLIC cxx_std_20)
    target_compile_definitions(khdays-ps5-vulkan-media-backend PUBLIC
        VK_NO_PROTOTYPES=1)
    target_include_directories(khdays-ps5-vulkan-media-backend PUBLIC
        platform/ps5
        "${KHDAYS_PS5_VULKAN_TEMPLATE_ROOT}/base"
        "${KHDAYS_PS5_VULKAN_TEMPLATE_ROOT}/external"
        "${KHDAYS_PS5_VULKAN_TEMPLATE_ROOT}/external/ktx/include"
        "${KHDAYS_PS5_VULKAN_TEMPLATE_ROOT}/external/ktx/other_include"
        "${KHDAYS_PS5_VULKAN_TEMPLATE_ROOT}/ps5/third_party/volk")
    # The final title supplies VulkanTexture.cpp/VulkanDevice.cpp and the volk
    # function pointers through PS5_VulkanTemplate's samples target.
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
        platform/ps5/asset_decode_bridge.cpp
        platform/ps5/runtime_media_pipeline.cpp)
    target_compile_features(khdays-ps5-asset-bridge PUBLIC cxx_std_20)
    target_include_directories(khdays-ps5-asset-bridge PUBLIC platform/ps5)
    target_link_libraries(khdays-ps5-asset-bridge PUBLIC khdays-pc-asset-decoders)

    add_executable(khdays-ps5-asset-smoke platform/ps5/asset_decode_smoke.cpp)
    target_link_libraries(khdays-ps5-asset-smoke PRIVATE khdays-ps5-asset-bridge)

    add_executable(khdays-ps5-media-smoke platform/ps5/runtime_media_smoke.cpp)
    target_link_libraries(khdays-ps5-media-smoke PRIVATE khdays-ps5-asset-bridge)

    if(KHDAYS_PS5_BUILD_VULKAN_MEDIA_BACKEND)
        add_library(khdays-ps5-native-media-runtime STATIC
            platform/ps5/native_media_runtime.cpp)
        target_compile_features(khdays-ps5-native-media-runtime PUBLIC cxx_std_20)
        target_link_libraries(khdays-ps5-native-media-runtime PUBLIC
            khdays-ps5-core
            khdays-ps5-asset-bridge
            khdays-ps5-vulkan-media-backend)

        if(KHDAYS_PS5_NATIVE_BACKEND)
            add_library(khdays-ps5-vulkan-title-program OBJECT
                platform/ps5/title/khdaysps5.cpp)
            target_compile_features(khdays-ps5-vulkan-title-program PUBLIC cxx_std_20)
            target_compile_definitions(khdays-ps5-vulkan-title-program PRIVATE
                VK_EXAMPLE_PS5=1
                _DIRECT2DISPLAY=1)
            target_include_directories(khdays-ps5-vulkan-title-program PRIVATE
                "${KHDAYS_PS5_VULKAN_TEMPLATE_ROOT}/external/glm"
                "${KHDAYS_PS5_VULKAN_TEMPLATE_ROOT}/external/imgui"
                "${KHDAYS_PS5_VULKAN_TEMPLATE_ROOT}/external/tinygltf")
            target_link_libraries(khdays-ps5-vulkan-title-program PRIVATE
                khdays-ps5-native-media-runtime)
        endif()
    endif()
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
