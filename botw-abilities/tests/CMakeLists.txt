cmake_minimum_required(VERSION 3.25)
project(botw_abilities_host_tests LANGUAGES CXX)

# Newer MSVC STL headers require a complete std::basic_ostream where doctest's own
# forward declarations once sufficed (string_view stringification fails otherwise).
add_compile_definitions(DOCTEST_CONFIG_USE_STD_HEADERS)

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
    test_champion_blocking.cpp test_champion_fury.cpp test_champion_hud.cpp test_champion_appearances.cpp
    test_gale_glide.cpp test_fury_targets.cpp test_runtime_frame.cpp test_private_effects.cpp
    test_wolf_follow.cpp test_wolf_frame.cpp test_wolf_control.cpp test_wolf_gear.cpp test_wolf_turning.cpp
    test_wolf_ground.cpp ../src/wolf/GroundMotion.cpp test_wolf_cliff.cpp ../src/wolf/CliffSearch.cpp
    test_wolf_turn_facing.cpp ../src/wolf/TurnFacing.cpp
    test_wolf_cliff_turn.cpp ../src/wolf/CliffTurn.cpp
    test_wolf_cliff_recovery.cpp test_wolf_companion.cpp ../src/wolf/Companion.cpp test_wolf_memory.cpp
    test_wolf_circle.cpp test_wolf_sound_cues.cpp
    test_text_format.cpp ../src/support/TextFormat.cpp
    ../src/wolf/FollowFrame.cpp ../src/wolf/FollowTurning.cpp)
if(NOT MSVC)
    set_source_files_properties(../src/wolf/FollowFrame.cpp ../src/wolf/FollowTurning.cpp ../src/wolf/GroundMotion.cpp ../src/wolf/CliffSearch.cpp ../src/wolf/TurnFacing.cpp
        PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
endif()
target_include_directories(botw_abilities_host_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
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

add_executable(wolf_cliff_controller_tests test_main.cpp test_wolf_cliff_controller.cpp
    ../src/wolf/CliffController.cpp ../src/wolf/CliffEscape.cpp)
target_include_directories(wolf_cliff_controller_tests PRIVATE "${DOCTEST_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs" "${CMAKE_CURRENT_SOURCE_DIR}/../../runtime-support/include")
target_compile_features(wolf_cliff_controller_tests PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(wolf_cliff_controller_tests PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(wolf_cliff_controller_tests PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
endif()
doctest_discover_tests(wolf_cliff_controller_tests)

add_executable(wolf_route_tests test_main.cpp test_wolf_route.cpp ../src/wolf/RouteMove.cpp)
target_include_directories(wolf_route_tests PRIVATE "${DOCTEST_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs" "${CMAKE_CURRENT_SOURCE_DIR}/../../runtime-support/include")
target_compile_features(wolf_route_tests PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(wolf_route_tests PRIVATE /W4 /WX /permissive- /EHsc)
else()
    target_compile_options(wolf_route_tests PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
endif()
doctest_discover_tests(wolf_route_tests)

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

set(runtime_support "${CMAKE_CURRENT_SOURCE_DIR}/../../runtime-support")
add_executable(botw_motion_aim_tests "${runtime_support}/tests/test_motion_aim.cpp")
target_include_directories(botw_motion_aim_tests PRIVATE "${DOCTEST_INCLUDE_DIR}"
    "${runtime_support}/include" "${runtime_support}/tests/stubs")
target_compile_features(botw_motion_aim_tests PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(botw_motion_aim_tests PRIVATE /W4 /WX /permissive- /EHsc /fp:strict)
else()
    target_compile_options(botw_motion_aim_tests PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
endif()
doctest_discover_tests(botw_motion_aim_tests)
