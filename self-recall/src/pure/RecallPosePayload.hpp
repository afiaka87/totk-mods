#pragma once
#include <array>
#include <span>
#include <algorithm>
#include "RecallPoseFrame.hpp"

namespace self_recall::pure {
inline constexpr unsigned kPosePayloadArenaBytes = 72u * 1024u * 1024u;
inline constexpr unsigned kPoseReadBufferCount = 16;
inline constexpr unsigned kPosePayloadMaxBytes = sizeof(RecordedPoseFrame) + 4;

struct PosePayloadBlock {
    std::uint32_t next = 0;
    std::byte bytes[1020];
};
static_assert(sizeof(PosePayloadBlock) == 1024);
inline constexpr unsigned kPosePayloadBlockCount = kPosePayloadArenaBytes / sizeof(PosePayloadBlock);

struct PosePayloadUsage {
    std::uint64_t liveBytes = 0, peakBytes = 0, liveAllocated = 0, peakAllocated = 0;
};

// Only the recorder allocates/frees. Slot claims protect immutable chains from reuse.
class PosePayloadStore {
public:
    explicit PosePayloadStore(std::span<PosePayloadBlock> blocks) : blocks_(blocks) {
        for (unsigned i = 0; i < blocks.size(); ++i) blocks[i].next = i + 1;
        free_ = blocks.empty() ? kEnd : 0;
        if (!blocks.empty()) blocks.back().next = kEnd;
        available_ = static_cast<unsigned>(blocks.size());
    }
    static unsigned blocksFor(unsigned bytes) { return (bytes + 1019u) / 1020u; }
    bool canReplace(unsigned oldBytes, unsigned newBytes) const {
        return blocksFor(newBytes) <= available_ + blocksFor(oldBytes);
    }
    unsigned store(std::span<const std::byte> input) {
        if (input.empty() || input.size() > kPosePayloadMaxBytes ||
            !canReplace(0, static_cast<unsigned>(input.size()))) return kEnd;
        const auto first = free_;
        unsigned offset = 0, last = kEnd;
        while (offset < input.size()) {
            auto& block = blocks_[free_];
            last = free_;
            free_ = block.next;
            const auto count = std::min<unsigned>(1020, static_cast<unsigned>(input.size()) - offset);
            std::memcpy(block.bytes, input.data() + offset, count);
            offset += count;
            --available_;
        }
        blocks_[last].next = kEnd;
        usage_.liveBytes += input.size();
        usage_.liveAllocated += blocksFor(static_cast<unsigned>(input.size())) * sizeof(PosePayloadBlock);
        usage_.peakBytes = std::max(usage_.peakBytes, usage_.liveBytes);
        usage_.peakAllocated = std::max(usage_.peakAllocated, usage_.liveAllocated);
        return first;
    }
    void release(unsigned first, unsigned bytes) {
        if (!bytes) return;
        unsigned remaining = blocksFor(bytes);
        while (remaining--) {
            auto& block = blocks_[first];
            const auto next = block.next;
            block.next = free_;
            free_ = first;
            first = next;
            ++available_;
        }
        usage_.liveBytes -= bytes;
        usage_.liveAllocated -= blocksFor(bytes) * sizeof(PosePayloadBlock);
    }
    bool load(unsigned first, unsigned bytes, std::span<std::byte> output) const {
        if (!bytes || bytes > output.size()) return false;
        unsigned offset = 0;
        while (offset < bytes) {
            if (first >= blocks_.size()) return false;
            const auto& block = blocks_[first];
            const auto count = std::min(1020u, bytes - offset);
            std::memcpy(output.data() + offset, block.bytes, count);
            offset += count;
            first = block.next;
        }
        return first == kEnd;
    }
    const PosePayloadUsage& usage() const { return usage_; }
private:
    static constexpr unsigned kEnd = UINT32_MAX;
    std::span<PosePayloadBlock> blocks_;
    unsigned free_ = kEnd, available_ = 0;
    PosePayloadUsage usage_{};
};

struct PoseDecodedFrame {
    std::atomic<bool> busy{false};
    RecordedPoseFrame frame{};
    std::array<std::byte, kPosePayloadMaxBytes> bytes{};
};

// Per-bone two-bit tags: exact +0, exact +1, or the original 32-bit word.
inline unsigned encodePosePayload(const PoseFrameInput& input, std::span<std::byte> output) {
    const auto& h = input.header;
    if (h.modelCount > kPoseModelLimit || h.boneCount > kPoseBoneLimit || !input.models || !input.bones) return 0;
    const unsigned modelBytes = h.modelCount * sizeof(RecordedModelPose);
    const unsigned boneBytes = h.boneCount * sizeof(RecordedBoneMatrix);
    unsigned compactBytes = 4 * h.boneCount;
    for (unsigned i = 0; i < h.boneCount; ++i)
        for (auto word : input.bones[i].words)
            if (word != 0 && word != 0x3f800000) compactBytes += 4;
    const std::uint32_t compact = compactBytes < boneBytes;
    const unsigned total = 4 + sizeof(RecordedVisibility) + modelBytes + (compact ? compactBytes : boneBytes);
    if (total > output.size()) return 0;
    auto* cursor = output.data();
    auto append = [&](const void* p, unsigned n) { std::memcpy(cursor, p, n); cursor += n; };
    append(&compact, 4);
    append(&input.visible, sizeof(input.visible));
    append(input.models, modelBytes);
    if (!compact) append(input.bones, boneBytes);
    else for (unsigned i = 0; i < h.boneCount; ++i) {
        std::uint32_t tags = 0;
        for (unsigned j = 0; j < 16; ++j) {
            const auto word = input.bones[i].words[j];
            tags |= (word == 0 ? 0u : word == 0x3f800000 ? 1u : 2u) << (2 * j);
        }
        append(&tags, 4);
        for (auto word : input.bones[i].words)
            if (word != 0 && word != 0x3f800000) append(&word, 4);
    }
    return total;
}

inline bool decodePosePayload(std::span<const std::byte> input, RecordedPoseFrame& frame) {
    const auto& h = frame.header;
    if (h.modelCount > kPoseModelLimit || h.boneCount > kPoseBoneLimit) return false;
    unsigned offset = 0;
    auto take = [&](void* p, unsigned n) {
        if (n > input.size() - offset) return false;
        std::memcpy(p, input.data() + offset, n); offset += n; return true;
    };
    std::uint32_t compact = 0;
    if (!take(&compact, 4) || compact > 1 || !take(&frame.visible, sizeof(frame.visible)) ||
        !take(frame.models, h.modelCount * sizeof(RecordedModelPose))) return false;
    if (!compact) {
        if (!take(frame.bones, h.boneCount * sizeof(RecordedBoneMatrix))) return false;
    } else for (unsigned i = 0; i < h.boneCount; ++i) {
        std::uint32_t tags = 0;
        if (!take(&tags, 4)) return false;
        for (unsigned j = 0; j < 16; ++j) {
            auto& word = frame.bones[i].words[j];
            switch ((tags >> (2 * j)) & 3) {
            case 0: word = 0; break;
            case 1: word = 0x3f800000; break;
            case 2: if (!take(&word, 4)) return false; break;
            default: return false;
            }
        }
    }
    return offset == input.size();
}
} // namespace self_recall::pure
