function(survey_add_packed_glyphs target)
    get_filename_component(packed_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    set(packed_cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/PackedGlyphData.cpp")
    add_custom_command(OUTPUT "${packed_cpp}"
        COMMAND uv run --no-project python "${packed_root}/tools/pack_glyph_tables.py"
            --source "${packed_root}/src/generated/GlyphTables.cpp" --output "${packed_cpp}"
        DEPENDS "${packed_root}/tools/pack_glyph_tables.py" "${packed_root}/src/generated/GlyphTables.cpp"
        VERBATIM)
    target_sources(${target} PRIVATE "${packed_cpp}")
endfunction()
