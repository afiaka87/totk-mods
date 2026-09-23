cmake_minimum_required(VERSION 3.25)
project(fleet_sync_host_tests LANGUAGES CXX)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
set(DOCTEST_DIR "${REPO_ROOT}/vendor/doctest")
if(NOT EXISTS "${DOCTEST_DIR}/doctest.h")
    include(FetchContent)
    FetchContent_Declare(doctest URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz)
    FetchContent_MakeAvailable(doctest)
    set(DOCTEST_DIR "${doctest_SOURCE_DIR}/doctest")
    set(DOCTEST_CMAKE "${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
else()
    set(DOCTEST_CMAKE "${DOCTEST_DIR}/doctest.cmake")
endif()

add_executable(linked_stick_pure_tests test_main.cpp test_matched.cpp test_lifecycle.cpp test_collision.cpp test_link_ux.cpp test_vehicle_match.cpp
    ../src/feature/FormationAssist.cpp ../src/feature/RiderInputDiagnostics.cpp
    ../src/feature/FleetTelemetry.cpp)
target_include_directories(linked_stick_pure_tests PRIVATE
    "${DOCTEST_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)
target_compile_features(linked_stick_pure_tests PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(linked_stick_pure_tests PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(linked_stick_pure_tests PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
include("${DOCTEST_CMAKE}")
doctest_discover_tests(linked_stick_pure_tests)
