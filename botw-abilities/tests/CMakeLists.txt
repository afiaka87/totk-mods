cmake_minimum_required(VERSION 3.25)
project(botw_abilities_host_tests LANGUAGES CXX)

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

add_executable(botw_abilities_host_tests test_main.cpp test_wheel_pages.cpp test_champions.cpp
    test_champion_healing.cpp test_champion_locations.cpp test_champion_combat.cpp test_champion_pose.cpp
    test_champion_audio.cpp test_champion_save.cpp test_champion_items.cpp
    test_champion_fatal.cpp test_champion_effects.cpp test_champion_gale.cpp test_champion_pose_matrices.cpp
    test_champion_blocking.cpp test_champion_fury.cpp test_champion_hud.cpp
    test_gale_glide.cpp test_fury_targets.cpp test_runtime_frame.cpp)
target_include_directories(botw_abilities_host_tests PRIVATE
    "${DOCTEST_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/wheel"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/champions"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../runtime-support/include"
)
target_compile_features(botw_abilities_host_tests PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(botw_abilities_host_tests PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(botw_abilities_host_tests PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
include("${DOCTEST_CMAKE}")
doctest_discover_tests(botw_abilities_host_tests)

set(remote_bomb_pure "${CMAKE_CURRENT_SOURCE_DIR}/../../totk-remote-bomb/src/pure")
if(NOT EXISTS "${remote_bomb_pure}")
    set(remote_bomb_pure "${CMAKE_CURRENT_SOURCE_DIR}/../../remote-bomb/src/pure")
endif()

add_executable(botw_native_sphere_tests test_main.cpp test_native_sphere_policy.cpp)
target_include_directories(botw_native_sphere_tests PRIVATE "${DOCTEST_INCLUDE_DIR}"
    "${remote_bomb_pure}")
target_compile_features(botw_native_sphere_tests PRIVATE cxx_std_23)
target_compile_definitions(botw_native_sphere_tests PRIVATE BOTW_ABILITIES_NATIVE_SPHERE=1)
if(MSVC)
    target_compile_options(botw_native_sphere_tests PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(botw_native_sphere_tests PRIVATE -Wall -Wextra -Werror)
endif()
doctest_discover_tests(botw_native_sphere_tests)

add_executable(botw_native_cube_tests test_main.cpp test_native_cube_policy.cpp)
target_include_directories(botw_native_cube_tests PRIVATE "${DOCTEST_INCLUDE_DIR}"
    "${remote_bomb_pure}")
target_compile_features(botw_native_cube_tests PRIVATE cxx_std_23)
target_compile_definitions(botw_native_cube_tests PRIVATE BOTW_ABILITIES_NATIVE_SPHERE=1 BOTW_ABILITIES_NATIVE_CUBE=1)
if(MSVC)
    target_compile_options(botw_native_cube_tests PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(botw_native_cube_tests PRIVATE -Wall -Wextra -Werror)
endif()
doctest_discover_tests(botw_native_cube_tests)
