cmake_minimum_required(VERSION 3.25)
project(zonai_ascend_host_tests LANGUAGES CXX)

# doctest: the monorepo's vendored copy when present, otherwise the pinned upstream release.
set(_vendored_doctest "${CMAKE_CURRENT_SOURCE_DIR}/../../../../vendor/doctest")
if(EXISTS "${_vendored_doctest}/doctest.h")
    set(DOCTEST_INCLUDE_DIR "${_vendored_doctest}")
    set(DOCTEST_CMAKE "${_vendored_doctest}/doctest.cmake")
else()
    include(FetchContent)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11)
    FetchContent_MakeAvailable(doctest)
    set(DOCTEST_INCLUDE_DIR "${doctest_SOURCE_DIR}/doctest")
    set(DOCTEST_CMAKE "${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
endif()

add_executable(zonai_ascend_pure_tests
    test_main.cpp
    test_reach_config.cpp
)
target_include_directories(zonai_ascend_pure_tests PRIVATE
    "${DOCTEST_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/pure"
)
target_compile_features(zonai_ascend_pure_tests PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(zonai_ascend_pure_tests PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(zonai_ascend_pure_tests PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
include("${DOCTEST_CMAKE}")
doctest_discover_tests(zonai_ascend_pure_tests)
