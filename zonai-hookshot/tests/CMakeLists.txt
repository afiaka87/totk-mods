cmake_minimum_required(VERSION 3.25)
project(zonai_hookshot_host_tests LANGUAGES CXX)

# Vendored doctest inside the monorepo, otherwise fetch the same pinned release.
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

add_executable(zonai_hookshot_pure_tests
    test_main.cpp
    test_target_validator.cpp
    test_hookshot_state.cpp
    test_chain_visual.cpp
    test_position_zip.cpp
    test_capture_math.cpp
    test_yaw_easing.cpp
    test_chain_presentation.cpp
    test_chain_projection.cpp
    test_hookshot_cues.cpp
    test_traversal_ownership.cpp
    test_handheld_pose.cpp
    test_travel_glide_pose.cpp
    test_travel_body_anchor.cpp
)
target_include_directories(zonai_hookshot_pure_tests PRIVATE
    "${DOCTEST_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/pure"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../arrowbound/src/pure"
)
target_compile_features(zonai_hookshot_pure_tests PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(zonai_hookshot_pure_tests PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(zonai_hookshot_pure_tests PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
include("${DOCTEST_CMAKE}")
doctest_discover_tests(zonai_hookshot_pure_tests)

set(runtime_support "${CMAKE_CURRENT_SOURCE_DIR}/../../runtime-support")
add_executable(hookshot_motion_tests "${runtime_support}/tests/test_motion_aim.cpp")
target_include_directories(hookshot_motion_tests PRIVATE "${DOCTEST_INCLUDE_DIR}"
    "${runtime_support}/include" "${runtime_support}/tests/stubs")
target_compile_features(hookshot_motion_tests PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(hookshot_motion_tests PRIVATE /W4 /WX /permissive- /EHsc /fp:strict)
else()
    target_compile_options(hookshot_motion_tests PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
endif()
doctest_discover_tests(hookshot_motion_tests)

# The combined product validates its imported feature with that feature's own suite.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../arrowbound/tests"
                 "${CMAKE_CURRENT_BINARY_DIR}/arrowbound")
