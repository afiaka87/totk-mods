cmake_minimum_required(VERSION 3.25)
project(self_recall_host_tests LANGUAGES C CXX)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
set(VENDORED_DOCTEST "${REPO_ROOT}/vendor/doctest")
if(EXISTS "${VENDORED_DOCTEST}/doctest.h")
    set(DOCTEST_INCLUDE_DIR "${VENDORED_DOCTEST}")
else()
    include(FetchContent)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11)
    FetchContent_MakeAvailable(doctest)
    set(DOCTEST_INCLUDE_DIR "${doctest_SOURCE_DIR}/doctest")
endif()

include(Compression.cmake)

set(SELF_RECALL_TEST_INCLUDES
    "${DOCTEST_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/pure"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/engine"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/support"
)

function(configure_self_recall_target target profile)
    target_include_directories(${target} PRIVATE ${SELF_RECALL_TEST_INCLUDES})
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_compile_definitions(${target} PRIVATE SELF_RECALL_STORAGE_PROFILE=${profile})
    target_link_libraries(${target} PRIVATE self_recall_codecs)
    if(MSVC)
        target_link_options(${target} PRIVATE /STACK:8388608)
        target_compile_options(${target} PRIVATE /W4 /WX /wd4324 /permissive- /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
endfunction()

set(SELF_RECALL_COMMON_TEST_SOURCES
    test_profile.cpp
    test_appearance.cpp
    test_control.cpp
    test_graphics.cpp
    test_model.cpp
    test_playback.cpp
    test_pose.cpp
    test_render.cpp
    ../src/engine/RecallModelAccess.cpp
)

add_executable(self_recall_pure_tests ${SELF_RECALL_COMMON_TEST_SOURCES})
configure_self_recall_target(self_recall_pure_tests 8)

add_executable(self_recall_switch_tests test_profile.cpp)
configure_self_recall_target(self_recall_switch_tests 7)

add_executable(recall_corpus_replay recall_corpus_replay.cpp)
configure_self_recall_target(recall_corpus_replay 7)

add_executable(recall_corpus_replay_emulator recall_corpus_replay.cpp)
configure_self_recall_target(recall_corpus_replay_emulator 8)

enable_testing()
add_test(NAME self-recall/emulator COMMAND $<TARGET_FILE:self_recall_pure_tests>)
add_test(NAME self-recall/switch-profile COMMAND $<TARGET_FILE:self_recall_switch_tests>)

set(SELF_RECALL_CORPUS "" CACHE FILEPATH "Optional retained pose corpus used by both replay profiles")
if(SELF_RECALL_CORPUS)
    get_filename_component(SELF_RECALL_CORPUS_PATH "${SELF_RECALL_CORPUS}" ABSOLUTE
                           BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT EXISTS "${SELF_RECALL_CORPUS_PATH}")
        message(FATAL_ERROR "SELF_RECALL_CORPUS does not exist: ${SELF_RECALL_CORPUS_PATH}")
    endif()
    add_test(NAME self-recall/corpus-emulator
             COMMAND $<TARGET_FILE:recall_corpus_replay_emulator> "${SELF_RECALL_CORPUS_PATH}")
    add_test(NAME self-recall/corpus-switch
             COMMAND $<TARGET_FILE:recall_corpus_replay> "${SELF_RECALL_CORPUS_PATH}")
endif()
