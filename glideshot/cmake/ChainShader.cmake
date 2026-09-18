# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis

# Compiles the chain pass GLSL to Maxwell binaries with the pinned external compiler and emits a generated header.
get_filename_component(HOOKSHOT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(HOOKSHOT_UAM "$ENV{HOOKSHOT_UAM}"
    CACHE FILEPATH "Pinned external NVN shader compiler")
set(HOOKSHOT_UAM_RUNTIME "$ENV{HOOKSHOT_UAM_RUNTIME}"
    CACHE PATH "External compiler runtime DLL directory")
set(HOOKSHOT_NVDISASM "$ENV{HOOKSHOT_NVDISASM}"
    CACHE FILEPATH "Pinned external Maxwell disassembler")

if(NOT EXISTS "${HOOKSHOT_UAM}" OR NOT EXISTS "${HOOKSHOT_NVDISASM}")
    message(FATAL_ERROR
        "The chain shader needs the pinned compiler pair. Set HOOKSHOT_UAM and "
        "HOOKSHOT_NVDISASM (and HOOKSHOT_UAM_RUNTIME for its DLLs) as cache or "
        "environment variables. Other uam builds are rejected by the hash pin in "
        "tools/compile_shaders.py.")
endif()

set(HOOKSHOT_SHADER_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/ChainShaders.hpp")
add_custom_command(OUTPUT "${HOOKSHOT_SHADER_HEADER}"
    COMMAND uv run --no-project python "${HOOKSHOT_ROOT}/tools/compile_shaders.py"
        --uam "${HOOKSHOT_UAM}" --runtime-dir "${HOOKSHOT_UAM_RUNTIME}"
        --nvdisasm "${HOOKSHOT_NVDISASM}"
        --source "${HOOKSHOT_ROOT}/shaders"
        --output "${CMAKE_CURRENT_BINARY_DIR}/generated"
    DEPENDS "${HOOKSHOT_ROOT}/tools/compile_shaders.py"
        "${HOOKSHOT_ROOT}/shaders/chain.vert"
        "${HOOKSHOT_ROOT}/shaders/chain.frag"
        "${HOOKSHOT_UAM}" "${HOOKSHOT_NVDISASM}"
    COMMENT "Compiling the chain and reticle shaders"
    VERBATIM)
add_custom_target(chain_shaders DEPENDS "${HOOKSHOT_SHADER_HEADER}")
add_dependencies(subsdk9 chain_shaders)
target_include_directories(subsdk9 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
