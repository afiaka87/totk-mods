#pragma once
#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

namespace self_recall::pure {
class PoseCompressor {
    alignas(8) std::array<std::byte, 300 * 1024> workspace_{};
    ZSTD_CCtx* context_ = nullptr;
public:
    unsigned compress(std::span<const std::byte> source, std::span<std::byte> output) {
        const auto parameters = ZSTD_getCParams(1, source.size(), 0);
        if (ZSTD_estimateCCtxSize_usingCParams(parameters) > workspace_.size()) return 0;
        if (!context_) context_ = ZSTD_initStaticCCtx(workspace_.data(), workspace_.size());
        if (!context_ || ZSTD_isError(ZSTD_CCtx_reset(context_, ZSTD_reset_session_and_parameters)) ||
            ZSTD_isError(ZSTD_CCtx_setCParams(context_, parameters))) return 0;
        const auto size = ZSTD_compress2(context_, output.data(), output.size(), source.data(), source.size());
        return ZSTD_isError(size) ? 0 : static_cast<unsigned>(size);
    }
};

class PoseDecompressor {
    alignas(8) std::array<std::byte, 96 * 1024> workspace_{};
    ZSTD_DCtx* context_ = nullptr;
public:
    bool decompress(std::span<const std::byte> source, std::span<std::byte> output) {
        if (ZSTD_estimateDCtxSize() > workspace_.size()) return false;
        if (!context_) context_ = ZSTD_initStaticDCtx(workspace_.data(), workspace_.size());
        return context_ && ZSTD_decompressDCtx(context_, output.data(), output.size(),
                                             source.data(), source.size()) == output.size();
    }
};
} // namespace self_recall::pure
