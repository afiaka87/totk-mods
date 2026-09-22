# Import the feature, never its standalone entry point or private host configuration.
function(arrowbound_import host_target)
    get_filename_component(arrow_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    file(GLOB_RECURSE arrow_sources CONFIGURE_DEPENDS
        "${arrow_root}/src/engine/*.cpp"
        "${arrow_root}/src/program/modules/arrowbound/*.cpp")
    add_library(arrowbound_feature OBJECT ${arrow_sources})
    option(ARROWBOUND_FLIGHT_DIAGNOSTICS "Read-only arrow, carrier and camera traces" OFF)
    target_compile_definitions(arrowbound_feature PRIVATE
        ARROWBOUND_FLIGHT_DIAGNOSTICS=$<BOOL:${ARROWBOUND_FLIGHT_DIAGNOSTICS}>)
    get_target_property(host_includes ${host_target} INCLUDE_DIRECTORIES)
    list(FILTER host_includes EXCLUDE REGEX "${PROJECT_SOURCE_DIR}/src($|/)")
    # Host lib/program settings must define the one linked exlaunch heap/hook pools.
    set_target_properties(arrowbound_feature PROPERTIES
        CXX_STANDARD 26
        INCLUDE_DIRECTORIES "${arrow_root}/src/include;${arrow_root}/src/pure;${arrow_root}/src/engine;${arrow_root}/src/support;${PROJECT_SOURCE_DIR}/src/lib;${host_includes}")
    target_sources(${host_target} PRIVATE $<TARGET_OBJECTS:arrowbound_feature>)
    target_include_directories(${host_target} PRIVATE "${arrow_root}/src/include")
endfunction()
