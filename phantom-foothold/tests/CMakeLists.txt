# Standalone host tests. The Switch build remains owned by ../CMakeLists.txt.
cmake_minimum_required(VERSION 3.25)
project(phantom_foothold_host_tests LANGUAGES CXX)

add_executable(phantom_launder_policy_test launder_policy_test.cpp)
target_include_directories(phantom_launder_policy_test PRIVATE ../src/program)
target_compile_features(phantom_launder_policy_test PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(phantom_launder_policy_test PRIVATE /W4 /WX /permissive- /EHsc /wd4127)
else()
    target_compile_options(phantom_launder_policy_test PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
add_test(NAME phantom_launder_policy COMMAND phantom_launder_policy_test)
