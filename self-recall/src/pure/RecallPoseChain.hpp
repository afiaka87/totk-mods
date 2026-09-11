#pragma once
#include "RecallPosePayload.hpp"

namespace self_recall::pure {
inline constexpr unsigned kPoseChainFrames = 8;
class PoseChainEncoder {
    PoseCompressor compressor_;
    std::array<std::byte, kPosePayloadMaxBytes> previous_{}, raw_{}, delta_{};
    unsigned previousBytes_ = 0, modelCount_ = 0, depth_ = 0;
    unsigned first_ = UINT32_MAX, storedBytes_ = 0;
    unsigned nextRawBytes_ = 0, nextModels_ = 0, nextDepth_ = 0;
public:
    void reset(PosePayloadStore& store) {
        if (storedBytes_) store.release(first_, storedBytes_);
        previousBytes_ = storedBytes_ = depth_ = 0;
        first_ = UINT32_MAX;
    }
    unsigned prepare(const PoseFrameInput& input, std::span<std::byte> output,
                     unsigned& parent, unsigned& parentBytes) {
        const auto bytes = encodePosePayload(input, raw_, true);
        if (!bytes || bytes + 8 > output.size()) return 0;
        bool compatible = previousBytes_ == bytes && modelCount_ == input.header.modelCount && depth_ + 1 < kPoseChainFrames;
        for (unsigned i = 0; compatible && i < modelCount_; ++i) {
            const auto offset = 4 + sizeof(RecordedVisibility) + i * sizeof(RecordedModelPose) + offsetof(RecordedModelPose, identity);
            compatible = std::memcmp(raw_.data() + offset, previous_.data() + offset, sizeof(RecordedModelIdentity)) == 0;
        }
        const auto* source = raw_.data();
        if (compatible) {
            for (unsigned i = 0; i < bytes; ++i) delta_[i] = raw_[i] ^ previous_[i];
            source = delta_.data();
        }
        const auto packed = compressor_.compress({source, bytes}, output.subspan(8, bytes));
        const unsigned header[2]{bytes, packed ? 1u : 0u};
        std::memcpy(output.data(), header, sizeof(header));
        if (!packed) std::memcpy(output.data() + 8, source, bytes);
        parent = compatible ? first_ : UINT32_MAX;
        parentBytes = compatible ? storedBytes_ : 0;
        nextRawBytes_ = bytes;
        nextModels_ = input.header.modelCount;
        nextDepth_ = compatible ? depth_ + 1 : 0;
        return 8 + (packed ? packed : bytes);
    }
    void commit(PosePayloadStore& store, unsigned first, unsigned bytes) {
        store.retain(first);
        if (storedBytes_) store.release(first_, storedBytes_);
        first_ = first;
        storedBytes_ = bytes;
        previousBytes_ = nextRawBytes_;
        modelCount_ = nextModels_;
        depth_ = nextDepth_;
        std::memcpy(previous_.data(), raw_.data(), previousBytes_);
    }
};

inline bool decodePoseChain(const PosePayloadStore& store, unsigned first, unsigned bytes,
                            PoseDecodedFrame& decoded, const PoseFrameHeader& header) {
    struct Node { unsigned first, bytes; };
    std::array<Node, kPoseChainFrames> nodes{};
    unsigned count = 0;
    while (bytes) {
        if (count == nodes.size()) return false;
        nodes[count++] = {first, bytes};
        if (!store.parent(first, first, bytes)) return false;
    }
    unsigned rawBytes = 0;
    const auto encoded = std::as_writable_bytes(std::span{&decoded.frame, 1});
    for (unsigned remaining = count; remaining; --remaining) {
        const auto node = nodes[remaining - 1];
        if (node.bytes < 8 || !store.load(node.first, node.bytes, encoded)) return false;
        unsigned info[2];
        std::memcpy(info, encoded.data(), sizeof(info));
        if (!info[0] || info[0] > decoded.raw.size() || info[1] > 1 ||
            (rawBytes && rawBytes != info[0])) return false;
        const auto source = encoded.subspan(8, node.bytes - 8);
        auto output = std::span{decoded.bytes}.first(info[0]);
        if (info[1]) {
            if (!decoded.decompressor.decompress(source, output)) return false;
        } else {
            if (source.size() != output.size()) return false;
            std::memcpy(output.data(), source.data(), source.size());
        }
        if (rawBytes) {
            for (unsigned i = 0; i < rawBytes; ++i) decoded.raw[i] ^= output[i];
        } else {
            rawBytes = info[0];
            std::memcpy(decoded.raw.data(), output.data(), rawBytes);
        }
    }
    decoded.frame.header = header;
    return rawBytes && decodePosePayload({decoded.raw.data(), rawBytes}, decoded.frame);
}
} // namespace self_recall::pure
