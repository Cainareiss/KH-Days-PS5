cmake_minimum_required(VERSION 3.23)
project(KHDaysPS5Core LANGUAGES CXX)

option(KHDAYS_PS5_BUILD_TESTS "Build the platform-neutral core smoke test" ON)

add_library(khdays-ps5-core STATIC
    core/src/frontend_core.cpp
    platform/ps5/frontend_session.cpp
)
target_compile_features(khdays-ps5-core PUBLIC cxx_std_20)
target_include_directories(khdays-ps5-core PUBLIC core/include ${CMAKE_CURRENT_SOURCE_DIR})

if(KHDAYS_PS5_BUILD_TESTS)
    enable_testing()
    add_executable(khdays-ps5-core-smoke tests/frontend_core_smoke.cpp)
    target_link_libraries(khdays-ps5-core-smoke PRIVATE khdays-ps5-core)
    add_test(NAME frontend_core_smoke COMMAND khdays-ps5-core-smoke)
endif()
