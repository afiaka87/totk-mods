cmake_minimum_required(VERSION 3.25)
project(zonai_fieldworks_host_tests LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_executable(zonai_fieldworks_switch_file_test switch_file_test.cpp)
target_include_directories(zonai_fieldworks_switch_file_test PRIVATE ../src ../../runtime-support/include)
target_compile_features(zonai_fieldworks_switch_file_test PRIVATE cxx_std_23)
target_compile_options(zonai_fieldworks_switch_file_test PRIVATE -Wall -Wextra -Werror)

# The shared-file merge code: a test on made-up inputs, and the tool scripts/merge_check.py runs on real files.
foreach(name merge_test merge_tool)
  add_executable(zonai_fieldworks_${name} ${name}.cpp)
  target_include_directories(zonai_fieldworks_${name} PRIVATE ../src)
  target_compile_features(zonai_fieldworks_${name} PRIVATE cxx_std_23)
  target_compile_options(zonai_fieldworks_${name} PRIVATE -Wall -Wextra -Werror)
endforeach()

enable_testing()
add_test(NAME zonai_fieldworks_switch_file COMMAND zonai_fieldworks_switch_file_test)
add_test(NAME zonai_fieldworks_merge COMMAND zonai_fieldworks_merge_test)

get_filename_component(HOST_COMPILER_RUNTIME "${CMAKE_CXX_COMPILER}" DIRECTORY)
set_tests_properties(zonai_fieldworks_switch_file zonai_fieldworks_merge PROPERTIES ENVIRONMENT_MODIFICATION
  "PATH=path_list_prepend:${HOST_COMPILER_RUNTIME}")
