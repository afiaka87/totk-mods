get_filename_component(RECALL_MOD_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(EXISTS "${RECALL_MOD_ROOT}/third_party/zstd/zstd.c")
    set(RECALL_ZSTD_INCLUDE "${RECALL_MOD_ROOT}/third_party/zstd")
    set(RECALL_LZ4_INCLUDE "${RECALL_MOD_ROOT}/third_party/lz4")
    set(RECALL_CODEC_SOURCES "${RECALL_ZSTD_INCLUDE}/zstd.c" "${RECALL_LZ4_INCLUDE}/lz4.c")
else()
    include(FetchContent)
    FetchContent_Declare(recall_zstd
        URL https://github.com/facebook/zstd/archive/refs/tags/v1.5.7.tar.gz
        URL_HASH SHA256=37d7284556b20954e56e1ca85b80226768902e2edabd3b649e9e72c0c9012ee3
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_Declare(recall_lz4
        URL https://github.com/lz4/lz4/archive/refs/tags/v1.10.0.tar.gz
        URL_HASH SHA256=537512904744b35e232912055ccf8ec66d768639ff3abe5788d90d792ec5f48b
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(recall_zstd recall_lz4)
    set(RECALL_ZSTD_INCLUDE "${recall_zstd_SOURCE_DIR}/lib")
    set(RECALL_LZ4_INCLUDE "${recall_lz4_SOURCE_DIR}/lib")
    file(GLOB RECALL_CODEC_SOURCES CONFIGURE_DEPENDS
        "${RECALL_ZSTD_INCLUDE}/common/*.c"
        "${RECALL_ZSTD_INCLUDE}/compress/*.c"
        "${RECALL_ZSTD_INCLUDE}/decompress/*.c")
    list(APPEND RECALL_CODEC_SOURCES "${RECALL_LZ4_INCLUDE}/lz4.c")
endif()

add_library(self_recall_codecs STATIC ${RECALL_CODEC_SOURCES})
set_target_properties(self_recall_codecs PROPERTIES C_STANDARD 11)
target_include_directories(self_recall_codecs PUBLIC "${RECALL_ZSTD_INCLUDE}" "${RECALL_LZ4_INCLUDE}")
target_compile_definitions(self_recall_codecs PRIVATE ZSTD_DISABLE_ASM=1 ZSTD_LEGACY_SUPPORT=0
    ZSTD_EXCLUDE_DFAST_BLOCK_COMPRESSOR
    ZSTD_EXCLUDE_GREEDY_BLOCK_COMPRESSOR
    ZSTD_EXCLUDE_LAZY_BLOCK_COMPRESSOR
    ZSTD_EXCLUDE_LAZY2_BLOCK_COMPRESSOR
    ZSTD_EXCLUDE_BTLAZY2_BLOCK_COMPRESSOR
    ZSTD_EXCLUDE_BTOPT_BLOCK_COMPRESSOR
    ZSTD_EXCLUDE_BTULTRA_BLOCK_COMPRESSOR)
if(NOT MSVC)
    target_compile_options(self_recall_codecs PRIVATE -w -ffunction-sections -fdata-sections)
endif()
