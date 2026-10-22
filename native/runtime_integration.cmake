# Build the real upstream runtime plus console services, separately from the
# decompiled game archive. This target does not emit a title executable.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(KHDAYS_NATIVE_RUNTIME_PORT "${CMAKE_BINARY_DIR}/runtime-sources")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../scripts/prepare_native_runtime_sources.py"
        --source "${KHDAYS_NATIVE_UPSTREAM_ROOT}" --out "${CMAKE_BINARY_DIR}/runtime-sources"
    COMMAND_ERROR_IS_FATAL ANY)
include("${CMAKE_CURRENT_LIST_DIR}/runtime_sources.cmake")

# Prepare the pinned game's compute renderers and committed SPIR-V for the
# native PS5 backend. This writes into the build directory and never edits the
# upstream checkout. The narrow SDL_GPU implementation below maps that API to
# RADV/Vulkan and owns a VK_KHR_display VideoOut swapchain.
set(KHDAYS_NATIVE_VULKAN_PORT "${CMAKE_BINARY_DIR}/vulkan-sources")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../scripts/prepare_native_vulkan_sources.py"
        --source "${KHDAYS_NATIVE_UPSTREAM_ROOT}" --out "${KHDAYS_NATIVE_VULKAN_PORT}"
    COMMAND_ERROR_IS_FATAL ANY)

# These are the exact revisions pinned by khdays-port's own CMakeLists.
FetchContent_Declare(khdays_pl_mpeg
    URL https://codeload.github.com/phoboslab/pl_mpeg/tar.gz/c871f2be022ece7ef4f64230b4fb8e1fb9eb6023
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR no-configure-ps5)
FetchContent_MakeAvailable(khdays_pl_mpeg)

set(KHDAYS_PS5_TEMPLATE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../toolchains/sources/PS5_VulkanTemplate" CACHE PATH
    "Public PS5 platform services used for scePad/sceAudioOut")
if(NOT EXISTS "${KHDAYS_PS5_TEMPLATE_ROOT}/ps5/src/platform.c")
    message(FATAL_ERROR "PS5 template platform services are missing")
endif()

add_library(khdays-native-ps5-runtime STATIC
    ${KHDAYS_NATIVE_PS5_RUNTIME_C_SOURCES}
    ps5/start_ps5.c ps5/rom_ps5.c ps5/paths_ps5.c
    ps5/host_services_ps5.c ps5/host_video_ps5.c ps5/host_io_ps5.c
    ps5/memory_ps5.c ps5/movie_hd_unavailable_ps5.c ps5/movie_subtitles_ps5.c
    ps5/sdl_gpu_vulkan.c ps5/vulkan_game_renderer.c ps5/camp_screen_ps5.c
    ps5/one_screen_host_stubs_ps5.c
    "${KHDAYS_NATIVE_UPSTREAM_ROOT}/port/host/render/onescreen.c"
    "${KHDAYS_NATIVE_VULKAN_PORT}/gpu_raster3d.c"
    "${KHDAYS_NATIVE_VULKAN_PORT}/gfx_render.c"
    "${KHDAYS_NATIVE_VULKAN_PORT}/gpu_hd.c"
    "${KHDAYS_PS5_TEMPLATE_ROOT}/ps5/third_party/volk/volk.c"
    "${KHDAYS_PS5_TEMPLATE_ROOT}/ps5/src/platform.c"
    "${CMAKE_CURRENT_LIST_DIR}/../toolchains/sources/PS5_PayloadSDK/platform/src/klog.c"
    "${CMAKE_CURRENT_LIST_DIR}/../toolchains/sources/PS5_Vulkan/radv/emutls_runtime.c"
    "${KHDAYS_NATIVE_GENERATED_ROOT}/ds_functions.c")

# The same default one-screen composition data the PC runtime embeds. It is
# available for the later accelerated presenter even in the reference path.
set(compositions "${KHDAYS_NATIVE_UPSTREAM_ROOT}/port/ui/compositions/port_days.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${compositions}")
file(READ "${compositions}" composition_hex HEX)
string(LENGTH "${composition_hex}" composition_digits)
math(EXPR composition_size "${composition_digits} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," composition_bytes "${composition_hex}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/compositions_port.c" CONTENT
"/* Generated from pinned upstream port_days.txt. */
const unsigned char khdays_compositions_port[] = {@composition_bytes@ 0};
const unsigned int khdays_compositions_port_size = @composition_size@u;
" @ONLY)
target_sources(khdays-native-ps5-runtime PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/compositions_port.c")
target_compile_features(khdays-native-ps5-runtime PRIVATE c_std_17)
target_include_directories(khdays-native-ps5-runtime PRIVATE
    "${KHDAYS_NATIVE_RUNTIME_PORT}"
    "${KHDAYS_NATIVE_RUNTIME_PORT}/host/app"
    "${KHDAYS_NATIVE_RUNTIME_PORT}/runtime/core"
    "${KHDAYS_NATIVE_RUNTIME_PORT}/runtime/movies"
    "${KHDAYS_NATIVE_RUNTIME_PORT}/runtime/net"
    "${KHDAYS_NATIVE_RUNTIME_PORT}/runtime/sound"
    "${KHDAYS_NATIVE_RUNTIME_PORT}/sound"
    "${KHDAYS_NATIVE_RUNTIME_PORT}/runtime/display"
    "${KHDAYS_NATIVE_RUNTIME_PORT}/runtime/mods"
    "${KHDAYS_NATIVE_RUNTIME_PORT}/hal"
    "${CMAKE_CURRENT_LIST_DIR}/ps5"
    "${CMAKE_CURRENT_LIST_DIR}/ps5/vulkan_compat"
    "${KHDAYS_NATIVE_VULKAN_PORT}"
    "${KHDAYS_NATIVE_VULKAN_PORT}/generated"
    "${KHDAYS_NATIVE_UPSTREAM_ROOT}/port"
    "${KHDAYS_NATIVE_UPSTREAM_ROOT}/port/host/render"
    "${KHDAYS_PS5_TEMPLATE_ROOT}/ps5/third_party/volk"
    "${CMAKE_CURRENT_LIST_DIR}/../toolchains/sources/PS5_Vulkan/.deps/native/radv-release/include"
    "${KHDAYS_NATIVE_GENERATED_ROOT}"
    "${KHDAYS_PS5_TEMPLATE_ROOT}/ps5/src"
    "${KHDAYS_NATIVE_UPSTREAM_ROOT}/external/khdays-decomp/libs/mobiclip/video/portable"
    "${khdays_stb_SOURCE_DIR}" "${khdays_dr_libs_SOURCE_DIR}"
    "${khdays_pl_mpeg_SOURCE_DIR}")
target_compile_options(khdays-native-ps5-runtime PRIVATE
    -fdeclspec -fno-strict-aliasing -fwrapv -fno-omit-frame-pointer
    -ffunction-sections -fdata-sections -Wall -Wextra
    -Wno-missing-field-initializers -Wno-unused-parameter)
target_link_libraries(khdays-native-ps5-runtime PUBLIC
    khdays-native-ps5-context khdays-native-ps5-host-contract
    khdays-native-ps5-game-tables khdays-native-ps5-gpu
    khdays-native-ps5-gfx khdays-native-ps5-sound)

enable_language(CXX)
include("${CMAKE_CURRENT_LIST_DIR}/runtime_assets.cmake")
