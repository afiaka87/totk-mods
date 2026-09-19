# Standalone host tests. The Switch build remains owned by ../CMakeLists.txt.
cmake_minimum_required(VERSION 3.25)
project(bivouac_host_tests LANGUAGES CXX)

add_executable(motherbase_solver_test
    motherbase_solver_test.cpp
    ../src/program/MotherBase.cpp
)
target_include_directories(motherbase_solver_test PRIVATE
    ../src/program
    stubs
)

add_executable(bivouac_policy_test bivouac_policy_test.cpp)
target_include_directories(bivouac_policy_test PRIVATE ../src/program)

add_executable(camp_ledger_codec_test
    camp_ledger_codec_test.cpp
    ../src/program/persistence/CampLedgerCodec.cpp
)
target_include_directories(camp_ledger_codec_test PRIVATE ../src/program)

add_executable(refund_service_test
    refund_service_test.cpp
    ../src/program/feature/RefundService.cpp
)
target_include_directories(refund_service_test PRIVATE ../src/program)

add_executable(raycast_service_test
    raycast_service_test.cpp
    ../src/program/engine/RaycastService.cpp
)
target_include_directories(raycast_service_test PRIVATE ../src/program)

add_executable(camp_geometry_test
    camp_geometry_test.cpp
    ../src/program/feature/CampGeometry.cpp
)
target_include_directories(camp_geometry_test PRIVATE ../src/program)

add_executable(camp_assembly_policy_test camp_assembly_policy_test.cpp)
target_include_directories(camp_assembly_policy_test PRIVATE ../src/program)

add_executable(arrival_watch_test arrival_watch_test.cpp)
target_include_directories(arrival_watch_test PRIVATE ../src/program)

foreach(target motherbase_solver_test bivouac_policy_test camp_ledger_codec_test refund_service_test raycast_service_test camp_geometry_test camp_assembly_policy_test arrival_watch_test)
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
endforeach()

enable_testing()
add_test(NAME motherbase_solver COMMAND motherbase_solver_test)
add_test(NAME bivouac_policy COMMAND bivouac_policy_test)
add_test(NAME camp_ledger_codec COMMAND camp_ledger_codec_test)
add_test(NAME refund_service COMMAND refund_service_test)
add_test(NAME raycast_service COMMAND raycast_service_test)
add_test(NAME camp_geometry COMMAND camp_geometry_test)
add_test(NAME camp_assembly_policy COMMAND camp_assembly_policy_test)
add_test(NAME arrival_watch COMMAND arrival_watch_test)
