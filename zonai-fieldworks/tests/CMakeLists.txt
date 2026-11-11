cmake_minimum_required(VERSION 3.25)
project(zonai_fieldworks_host_tests LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_executable(zonai_fieldworks_switch_file_test switch_file_test.cpp)
target_include_directories(zonai_fieldworks_switch_file_test PRIVATE ../src)
target_compile_features(zonai_fieldworks_switch_file_test PRIVATE cxx_std_23)
target_compile_options(zonai_fieldworks_switch_file_test PRIVATE -Wall -Wextra -Werror)

enable_testing()
add_test(NAME zonai_fieldworks_switch_file COMMAND zonai_fieldworks_switch_file_test)

get_filename_component(HOST_COMPILER_RUNTIME "${CMAKE_CXX_COMPILER}" DIRECTORY)
set_tests_properties(zonai_fieldworks_switch_file PROPERTIES ENVIRONMENT_MODIFICATION
  "PATH=path_list_prepend:${HOST_COMPILER_RUNTIME}")
