cmake_minimum_required(VERSION 3.25)
project(stasis_host_tests LANGUAGES CXX)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
set(VENDORED_DOCTEST "${REPO_ROOT}/vendor/doctest")
if(EXISTS "${VENDORED_DOCTEST}/doctest.h")
    set(DOCTEST_INCLUDE_DIR "${VENDORED_DOCTEST}")
    set(DOCTEST_CMAKE "${VENDORED_DOCTEST}/doctest.cmake")
else()
    include(FetchContent)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11)
    FetchContent_MakeAvailable(doctest)
    set(DOCTEST_INCLUDE_DIR "${doctest_SOURCE_DIR}/doctest")
    set(DOCTEST_CMAKE "${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
endif()

add_executable(stasis_host_tests
    test_main.cpp
    test_original_charge.cpp
    test_release.cpp
    test_projection.cpp
    test_camera.cpp
    test_target.cpp
    test_target_layers.cpp
    test_enemy_hold.cpp
    test_equipment.cpp
    test_hit_adapter.cpp
    test_trail.cpp
    test_input.cpp
    test_render_route.cpp
    ../src/engine/StasisTarget.cpp
)
target_include_directories(stasis_host_tests PRIVATE
    "${DOCTEST_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/pure"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/engine"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../runtime-support/include"
)
target_compile_features(stasis_host_tests PRIVATE cxx_std_23)
if(MSVC)
    # The raycast query is 16-byte aligned on purpose.
    target_compile_options(stasis_host_tests PRIVATE /W4 /WX /wd4324 /permissive- /EHsc /fp:strict)
else()
    target_compile_options(stasis_host_tests PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
endif()

enable_testing()
include("${DOCTEST_CMAKE}")
doctest_discover_tests(stasis_host_tests)
