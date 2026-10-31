cmake_minimum_required(VERSION 3.21)
project(material_columns_host LANGUAGES CXX)
enable_testing()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
foreach(name category_config localization version_detection game_version game_version_142 memory_profile_142 cursor_selection_142 cursor_selection equipment_input native_inventory column_selection inventory_columns resource_routing slot_observation selection_state navigation runtime_adapter panel_binding outer_pool state_codec state_journal scroll_motion sort_panel memory_profile layout_resources layout_resources_142 diagnostic_log)
    add_executable(${name} ${name}.cpp)
    target_include_directories(${name} PRIVATE stubs)
    if(MSVC)
        target_compile_options(${name} PRIVATE /W4 /UNDEBUG /utf-8)
        target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -pedantic -UNDEBUG)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endforeach()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../config/botw-abilities.ini")
    add_test(NAME category_files COMMAND category_config
        "${CMAKE_CURRENT_SOURCE_DIR}/../config/botw-abilities.ini"
        "${CMAKE_CURRENT_SOURCE_DIR}/../config/fuseables-ex.ini"
        "${CMAKE_CURRENT_SOURCE_DIR}/../config/invisible-attachments.ini"
        "${CMAKE_CURRENT_SOURCE_DIR}/../config/second-quest.ini"
        "${CMAKE_CURRENT_SOURCE_DIR}/../docs/examples/fictional-materials.ini")
endif()
