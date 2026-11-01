cmake_minimum_required(VERSION 3.25)
project(descend_host_tests LANGUAGES CXX)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
set(DOCTEST_DIR "${REPO_ROOT}/vendor/doctest" CACHE PATH "Directory containing doctest.h and doctest.cmake")
if(NOT EXISTS "${DOCTEST_DIR}/doctest.h")
    include(FetchContent)
    FetchContent_Declare(doctest URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz)
    FetchContent_MakeAvailable(doctest)
    set(DOCTEST_DIR "${doctest_SOURCE_DIR}/doctest")
    set(DOCTEST_CMAKE "${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
else()
    set(DOCTEST_CMAKE "${DOCTEST_DIR}/doctest.cmake")
endif()

add_executable(descend_pure_tests test_main.cpp)
target_include_directories(descend_pure_tests PRIVATE
    "${DOCTEST_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/pure"
)
target_compile_features(descend_pure_tests PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(descend_pure_tests PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(descend_pure_tests PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
include("${DOCTEST_CMAKE}")
doctest_discover_tests(descend_pure_tests)
