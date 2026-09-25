cmake_minimum_required(VERSION 3.25)
project(zonai_survey_host_tests LANGUAGES CXX)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
set(DOCTEST_DIR "${REPO_ROOT}/vendor/doctest")
if(EXISTS "${DOCTEST_DIR}/doctest.h")
    set(DOCTEST_CMAKE "${DOCTEST_DIR}/doctest.cmake")
else()
    include(FetchContent)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11)
    FetchContent_MakeAvailable(doctest)
    set(DOCTEST_DIR "${doctest_SOURCE_DIR}/doctest")
    set(DOCTEST_CMAKE "${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
endif()
set(SURVEY_DATA_DEFAULT OFF)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../src/generated/GlyphTables.cpp")
    set(SURVEY_DATA_DEFAULT ON)
endif()
option(SURVEY_TEST_GAME_DATA "Validate locally generated collectible tables" ${SURVEY_DATA_DEFAULT})

add_executable(zonai_survey_pure_tests
    test_main.cpp
    test_pulse_lattice.cpp
    test_scan_batching.cpp
    test_scan_reduction.cpp
    test_scan_verdicts.cpp
    test_wall_incidence.cpp
    test_wall_slope.cpp
    test_wall_survey.cpp
    test_survey_composition.cpp
    test_survey_sweep.cpp
    test_survey_cue.cpp
    test_depth_lift.cpp
    test_perf_report.cpp
    test_atlas_layout.cpp
)
if(SURVEY_TEST_GAME_DATA)
    target_sources(zonai_survey_pure_tests PRIVATE test_glyph_tables.cpp test_packed_glyphs.cpp
        ../src/generated/GlyphTables.cpp)
    include(../cmake/PackedGlyphs.cmake)
    survey_add_packed_glyphs(zonai_survey_pure_tests)
    target_compile_definitions(zonai_survey_pure_tests PRIVATE SURVEY_TEST_GAME_DATA=1)
else()
    message(STATUS "Game-data tests disabled: public source omits collectible tables; pure and rendering tests remain enabled")
endif()
target_include_directories(zonai_survey_pure_tests PRIVATE
    "${DOCTEST_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/pure"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/presentation"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/generated"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../runtime-support/include"
)
target_compile_features(zonai_survey_pure_tests PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(zonai_survey_pure_tests PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(zonai_survey_pure_tests PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
add_subdirectory(surface)
include("${DOCTEST_CMAKE}")
doctest_discover_tests(zonai_survey_pure_tests)
if(MINGW)
    set_property(TARGET zonai_survey_pure_tests APPEND PROPERTY LINK_OPTIONS -static)
endif()
