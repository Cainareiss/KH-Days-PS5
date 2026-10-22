# The upstream runtime's C++ data/overlay helpers belong to the same archive:
# the C runtime calls these exact exports; compiling the C half alone leaves
# movies, model names and the in-game save UI disconnected at link time.
if(NOT TARGET khdays-native-ps5-runtime)
    message(FATAL_ERROR "runtime_assets.cmake needs khdays-native-ps5-runtime first")
endif()
if(NOT CMAKE_CXX_COMPILER_LOADED)
    enable_language(CXX)
endif()

include(FetchContent)
FetchContent_Declare(khdays_cgltf
    URL https://codeload.github.com/jkuhlmann/cgltf/tar.gz/85cd62382dfea638278962690cf515023f33ed00
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR no-configure-ps5)
FetchContent_MakeAvailable(khdays_cgltf)
# Upstream pins each downloaded ufbx file by SHA-256 and the exact commit.
include("${KHDAYS_NATIVE_UPSTREAM_ROOT}/assets/fetch_ufbx.cmake")

set(khdays_runtime_cpp
    runtime/core/text_patches.cpp
    runtime/mods/names.cpp runtime/mods/models.cpp
    ui/save_numbers.cpp ui/room_icons.cpp ui/player_names.cpp ui/pause.cpp)
foreach(source IN LISTS khdays_runtime_cpp)
    if(NOT EXISTS "${KHDAYS_NATIVE_RUNTIME_PORT}/${source}")
        message(FATAL_ERROR "Missing portable C++ runtime source: ${source}")
    endif()
    target_sources(khdays-native-ps5-runtime PRIVATE
        "${KHDAYS_NATIVE_RUNTIME_PORT}/${source}")
endforeach()

set(khdays_asset_names
    animation audio cell fbx filesystem font gltf gltf_export graphics2d image mesh message)
foreach(name IN LISTS khdays_asset_names)
    target_sources(khdays-native-ps5-runtime PRIVATE
        "${KHDAYS_NATIVE_UPSTREAM_ROOT}/assets/src/${name}.cpp")
endforeach()
target_sources(khdays-native-ps5-runtime PRIVATE "${ufbx_SOURCE_DIR}/ufbx.c")

set(khdays_mobiclip
    "${KHDAYS_NATIVE_UPSTREAM_ROOT}/external/khdays-decomp/libs/mobiclip/video/portable")
target_sources(khdays-native-ps5-runtime PRIVATE
    "${khdays_mobiclip}/mobiclip_frame_core.cpp"
    "${khdays_mobiclip}/mobiclip_reference.cpp")
target_include_directories(khdays-native-ps5-runtime PRIVATE
    "${KHDAYS_NATIVE_UPSTREAM_ROOT}/assets/include"
    "${khdays_mobiclip}"
    "${khdays_stb_SOURCE_DIR}" "${khdays_cgltf_SOURCE_DIR}" "${ufbx_SOURCE_DIR}")
target_compile_features(khdays-native-ps5-runtime PRIVATE cxx_std_20)
# The runtime's UTF-16 literals are UTF-8 on disk; target wchar_t remains the
# platform's own type. No Windows wchar_t layout is imposed on the decoders.
target_compile_options(khdays-native-ps5-runtime PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-finput-charset=UTF-8;-fexceptions;-fcxx-exceptions>")
