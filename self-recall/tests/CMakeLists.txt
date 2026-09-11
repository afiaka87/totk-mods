cmake_minimum_required(VERSION 3.25)
project(self_recall_host_tests LANGUAGES C CXX)

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

add_executable(self_recall_pure_tests
    test_main.cpp
    test_recall_logic.cpp
    test_recall_visual.cpp
    test_recall_history.cpp
    test_recall_pose_history.cpp
    test_recall_pose_payload.cpp
    test_recall_pose_transport.cpp
    test_recall_model_capture.cpp
    test_recall_model_completion.cpp
    test_recall_frame_ticket.cpp
    test_recall_game_time.cpp
    test_recall_camera_frame.cpp
    test_recall_pose_playback.cpp
    test_recall_outfit_speed.cpp
    test_recall_route_probe_plan.cpp
    test_recall_render_frames.cpp
    test_recall_animation_visibility.cpp
    test_recall_native_admission.cpp
    test_recall_equipment_links.cpp
    test_recall_vehicle.cpp
    test_recall_equipment_effects.cpp
    test_recall_appearance.cpp
    test_recall_lossless.cpp
    test_recall_memory_profile.cpp
    test_recall_stop_policy.cpp
    test_recall_stamina.cpp
    test_recall_motion_continuity.cpp
    test_recall_wrist_provider.cpp
    test_recall_scene_material.cpp
    test_recall_monochrome.cpp
    test_recall_gpu_buffer.cpp
    test_recall_glider_release.cpp
    test_recall_gpu_lifetime.cpp
    test_recall_native_path.cpp
    ../src/engine/RecallModelView.cpp
)
# Pose tests use the mod-local support headers.
target_include_directories(self_recall_pure_tests PRIVATE
    "${DOCTEST_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/pure"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/engine"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/support"
)
target_compile_features(self_recall_pure_tests PRIVATE cxx_std_23)
target_compile_definitions(self_recall_pure_tests PRIVATE SELF_RECALL_MEMORY_PROFILE=1)
if(MSVC)
    # Match the usual host stack budget for fixtures containing bounded reader buffers.
    target_link_options(self_recall_pure_tests PRIVATE /STACK:8388608)
    target_compile_options(self_recall_pure_tests PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(self_recall_pure_tests PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
include("${DOCTEST_CMAKE}")
doctest_discover_tests(self_recall_pure_tests)

add_executable(self_recall_lossless_tests $<TARGET_PROPERTY:self_recall_pure_tests,SOURCES>)
target_include_directories(self_recall_lossless_tests PRIVATE $<TARGET_PROPERTY:self_recall_pure_tests,INCLUDE_DIRECTORIES>)
target_compile_features(self_recall_lossless_tests PRIVATE cxx_std_23)
target_compile_definitions(self_recall_lossless_tests PRIVATE SELF_RECALL_MEMORY_PROFILE=1 SELF_RECALL_STORAGE_PROFILE=6)
target_compile_options(self_recall_lossless_tests PRIVATE $<TARGET_PROPERTY:self_recall_pure_tests,COMPILE_OPTIONS>)
target_link_options(self_recall_lossless_tests PRIVATE $<TARGET_PROPERTY:self_recall_pure_tests,LINK_OPTIONS>)
doctest_discover_tests(self_recall_lossless_tests TEST_PREFIX "lossless/")

include(Compression.cmake)
add_executable(self_recall_compressed_tests test_main.cpp test_recall_compression.cpp)
target_include_directories(self_recall_compressed_tests PRIVATE $<TARGET_PROPERTY:self_recall_pure_tests,INCLUDE_DIRECTORIES>)
target_compile_features(self_recall_compressed_tests PRIVATE cxx_std_23)
target_compile_definitions(self_recall_compressed_tests PRIVATE SELF_RECALL_MEMORY_PROFILE=1 SELF_RECALL_STORAGE_PROFILE=7)
target_compile_options(self_recall_compressed_tests PRIVATE $<TARGET_PROPERTY:self_recall_pure_tests,COMPILE_OPTIONS>)
target_link_options(self_recall_compressed_tests PRIVATE $<TARGET_PROPERTY:self_recall_pure_tests,LINK_OPTIONS>)
target_link_libraries(self_recall_compressed_tests PRIVATE self_recall_codecs)
doctest_discover_tests(self_recall_compressed_tests TEST_PREFIX "compressed/")
add_executable(recall_corpus_replay recall_corpus_replay.cpp)
target_include_directories(recall_corpus_replay PRIVATE $<TARGET_PROPERTY:self_recall_pure_tests,INCLUDE_DIRECTORIES>)
target_compile_features(recall_corpus_replay PRIVATE cxx_std_23)
target_compile_definitions(recall_corpus_replay PRIVATE SELF_RECALL_MEMORY_PROFILE=1 SELF_RECALL_STORAGE_PROFILE=7)
target_link_libraries(recall_corpus_replay PRIVATE self_recall_codecs)
