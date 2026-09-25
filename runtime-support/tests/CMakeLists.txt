# Standalone host tests for the shared runtime-support pure layer.
# The Switch compile check is owned by ../switch-build/CMakeLists.txt.
cmake_minimum_required(VERSION 3.25)
project(runtime_support_host_tests LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
set(DOCTEST_DIR "${REPO_ROOT}/vendor/doctest")
if(NOT EXISTS "${DOCTEST_DIR}/doctest.h")
    include(FetchContent)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11)
    FetchContent_MakeAvailable(doctest)
    set(DOCTEST_DIR "${doctest_SOURCE_DIR}/doctest")
endif()

set(GLIDESHOT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../zonai-hookshot/src")
if(NOT EXISTS "${GLIDESHOT_SOURCE}/engine/GripHooks.hpp")
    set(GLIDESHOT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../glideshot/src")
endif()

enable_testing()
set(SUPPORT_TEST_TARGETS)
function(support_test name source)
    set(target "runtime_support_${name}_test")
    add_executable(${target} ${source})
    target_include_directories(${target} PRIVATE ../include "${DOCTEST_DIR}")
    target_compile_features(${target} PRIVATE cxx_std_23)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME "runtime_support_${name}" COMMAND ${target})
    set(SUPPORT_TEST_TARGETS ${SUPPORT_TEST_TARGETS} ${target} PARENT_SCOPE)
endfunction()

# The public source export contains only the three coexistence suites.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_overlay_layout.cpp")
    support_test(overlay_layout test_overlay_layout.cpp)
    support_test(harness_contract test_harness_contract.cpp)
    support_test(scene_rosters test_scene_rosters.cpp)
    support_test(input_raycast test_input_raycast.cpp)
    support_test(transform_gamedata test_transform_gamedata.cpp)
    support_test(render_geometry test_primitive_geometry.cpp)
endif()

support_test(pfx_hook test_pfx_hook.cpp)
support_test(grip_hook test_grip_hook.cpp)
support_test(flight_clock_hook test_flight_clock_hook.cpp)
target_include_directories(runtime_support_grip_hook_test PRIVATE "${GLIDESHOT_SOURCE}/engine")
target_include_directories(runtime_support_flight_clock_hook_test PRIVATE ../../arrowbound/src/engine)
foreach(name pfx_hook grip_hook flight_clock_hook)
    target_include_directories(runtime_support_${name}_test PRIVATE stubs)
    if(NOT MSVC)
        # Model AArch64 instruction-aligned callbacks on the host.
        target_compile_options(runtime_support_${name}_test PRIVATE -falign-functions=4)
    endif()
endforeach()

get_filename_component(HOST_COMPILER_RUNTIME "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_property(SUPPORT_TESTS DIRECTORY PROPERTY TESTS)
set_tests_properties(${SUPPORT_TESTS} PROPERTIES ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:${HOST_COMPILER_RUNTIME}")
