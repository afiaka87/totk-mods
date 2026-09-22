cmake_minimum_required(VERSION 3.25)
project(arrowbound_host_tests LANGUAGES CXX)

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

add_executable(arrowbound_pure_tests
    test_main.cpp
    test_arrow_hookshot.cpp
    test_arrow_identity.cpp
    test_carrier_save.cpp
    test_wall_grip.cpp
    test_code_only_emblem.cpp
    test_settings_io.cpp
    test_flight_diagnostics.cpp
    test_flight_clock.cpp
    test_ragdoll_transport.cpp
    ../src/program/modules/arrowbound/WallGripService.cpp
    ../src/engine/HookshotInput.cpp
    ../src/engine/ArrowSettings.cpp
)
target_include_directories(arrowbound_pure_tests PRIVATE
    "${DOCTEST_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/pure"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/engine"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/support"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
)
target_compile_features(arrowbound_pure_tests PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(arrowbound_pure_tests PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(arrowbound_pure_tests PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
include("${DOCTEST_CMAKE}")
doctest_discover_tests(arrowbound_pure_tests)
