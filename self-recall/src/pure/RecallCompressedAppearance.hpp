#pragma once
#include "RecallAppearanceHistory.hpp"
#include <lz4.h>

namespace self_recall::pure {
template<unsigned Blocks, unsigned States> class CompressedAppearanceBlobs {
    AppearanceBlobs<Blocks, States> blobs_;
    alignas(8) mutable std::array<std::byte, LZ4_STREAM_MINSIZE> encoder_{};
    mutable std::array<std::byte, 65536 + 8> scratch_{};
    unsigned encode(std::span<const std::byte> input) const {
        if (input.empty() || input.size() > 65536) return 0;
        const auto packed = LZ4_compress_fast_extState(encoder_.data(),
            reinterpret_cast<const char*>(input.data()), reinterpret_cast<char*>(scratch_.data() + 8),
            static_cast<int>(input.size()), static_cast<int>(input.size()), 1);
        const unsigned header[2]{static_cast<unsigned>(input.size()), packed > 0 ? 1u : 0u};
        std::memcpy(scratch_.data(), header, sizeof(header));
        if (!packed) std::memcpy(scratch_.data() + 8, input.data(), input.size());
        return 8 + (packed ? static_cast<unsigned>(packed) : static_cast<unsigned>(input.size()));
    }
public:
    void initialize() { blobs_.initialize(); }
    unsigned availableBytes() const { return blobs_.availableBytes(); }
    bool retain(unsigned token) { return blobs_.retain(token); }
    void release(unsigned token) { blobs_.release(token); }
#if SELF_RECALL_MEMORY_PROFILE
    const PoolMemoryUsage& memoryUsage() const { return blobs_.memoryUsage(); }
#endif
    unsigned size(unsigned token) const {
        auto reader = blobs_.reader(token);
        unsigned size = 0;
        return reader.copy(std::as_writable_bytes(std::span{&size, 1})) ? size : 0;
    }
    bool equal(unsigned token, std::span<const std::byte> input) const {
        if (input.size() != size(token)) return false;
        const auto bytes = encode(input);
        return bytes && blobs_.equal(token, {scratch_.data(), bytes});
    }
    unsigned create(std::span<const std::byte> input) {
        const auto bytes = encode(input);
        return bytes ? blobs_.create({scratch_.data(), bytes}) : 0;
    }
    bool copy(unsigned token, std::span<std::byte> output) const {
        auto reader = blobs_.reader(token);
        unsigned header[2]{};
        if (!reader.copy(std::as_writable_bytes(std::span{header})) || header[0] != output.size() ||
            header[1] > 1) return false;
        if (!header[1]) return reader.copy(output) && !reader.remaining();
        unsigned written = 0;
        const auto length = [&](unsigned initial, unsigned& result) {
            result = initial;
            if (initial != 15) return true;
            unsigned byte = 0;
            do {
                if (!reader.get(byte) || result > output.size() || byte > output.size() - result) return false;
                result += byte;
            } while (byte == 255);
            return true;
        };
        while (reader.remaining()) {
            unsigned command = 0, literals = 0;
            if (!reader.get(command) || !length(command >> 4, literals) ||
                literals > output.size() - written || !reader.copy(output.subspan(written, literals))) return false;
            written += literals;
            if (!reader.remaining()) return written == output.size();
            unsigned low = 0, high = 0, match = 0;
            if (!reader.get(low) || !reader.get(high) || !length(command & 15, match)) return false;
            const auto distance = low | (high << 8);
            if (!distance || distance > written || output.size() - written < 4 ||
                match > output.size() - written - 4) return false;
            match += 4;
            for (unsigned i = 0; i < match; ++i) {
                output[written] = output[written - distance];
                ++written;
            }
        }
        return false;
    }
};
} // namespace self_recall::pure
