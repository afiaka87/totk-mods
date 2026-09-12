#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

#include "RecallBase.hpp"

namespace self_recall::pure {

inline constexpr std::uint16_t kPoseModelLimit = 32;
inline constexpr std::uint16_t kPoseBoneLimit = 512;
inline constexpr std::uint16_t kPoseMaterialLimit = 512;
inline constexpr std::size_t kPoseHistoryByteLimit = 80u * 1024u * 1024u;

struct alignas(16) RecordedBoneMatrix {
    std::uint32_t words[16]{};
};
static_assert(sizeof(RecordedBoneMatrix) == 64);

struct RecordedModelIdentity {
    std::uint64_t unit = 0;
    std::uint64_t skeleton = 0;
    std::uint64_t resource = 0;
    std::uint16_t firstBone = 0;
    std::uint16_t boneCount = 0;
    std::uint16_t firstMaterial = 0;
    std::uint16_t materialCount = 0;
};
static_assert(sizeof(RecordedModelIdentity) == 32);

struct RecordedModelPose {
    float renderOrigin[3]{};
    std::uint32_t visibility = 0;
    std::uint32_t originRelative = 0;
    std::uint32_t queueAdmission = 0;
    RecordedModelIdentity identity{};
};
static_assert(sizeof(RecordedModelPose) == 56);

struct PoseFrameKey {
    std::uint64_t serial = 0;
    std::uint32_t generation = 0;
    std::uint32_t slot = 0;

    bool operator==(const PoseFrameKey&) const = default;
    explicit operator bool() const { return serial != 0 && generation != 0; }
};

struct PoseFrameHeader {
    PoseFrameKey key{};
    std::uint64_t frameEpoch = 0;
    std::uint64_t elapsedNanoseconds = 0;
    std::uint32_t worldGeneration = 0;
    std::uint32_t modelGeneration = 0;
    HistorySample route{};
    float wristMatrix[12]{};
    std::uint16_t modelCount = 0;
    std::uint16_t boneCount = 0;
    std::uint16_t materialCount = 0;
    bool haveWrist = false;
    std::uint8_t bodyModelCount = 0;
    std::uint8_t reserved[8]{};
    float waterHeight = 0;
    bool haveWaterHeight = false;
    std::uint8_t waterPadding[11]{};
};
static_assert(sizeof(PoseFrameHeader) % 16 == 0);

struct RecordedVisibility {
    std::uint32_t bones[kPoseBoneLimit / 32]{};
    std::uint32_t materials[kPoseMaterialLimit / 32]{};
};
static_assert(sizeof(RecordedVisibility) == 128);

inline bool visibilityBit(const std::uint32_t* bits, std::uint16_t index) {
    return (bits[index / 32] & (1u << (index % 32))) != 0;
}

struct RecordedPoseFrame {
    PoseFrameHeader header{};
    RecordedModelPose models[kPoseModelLimit]{};
    RecordedBoneMatrix bones[kPoseBoneLimit]{};
    RecordedVisibility visible{};
};

struct PoseFrameInput {
    PoseFrameHeader header{};
    const RecordedModelPose* models = nullptr;
    const RecordedBoneMatrix* bones = nullptr;
    RecordedVisibility visible{};
};

}

#include <array>
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
}

#include <algorithm>
namespace self_recall::pure {
inline constexpr unsigned kPoseReadBufferCount = 16;
inline constexpr unsigned kPosePayloadMaxBytes = sizeof(RecordedPoseFrame) + 4;
inline constexpr unsigned kPoseBlockDataBytes = 1008;

struct PosePayloadBlock {
    std::uint32_t next = 0;
    std::uint32_t refs = 0, parent = UINT32_MAX, parentBytes = 0;
    std::byte bytes[kPoseBlockDataBytes];
};
static_assert(sizeof(PosePayloadBlock) == 1024);
inline constexpr unsigned kPosePayloadBlockCount = kPosePayloadArenaBytes / sizeof(PosePayloadBlock);

// The recorder owns allocation; reader claims keep payload chains immutable.
class PosePayloadStore {
public:
    explicit PosePayloadStore(std::span<PosePayloadBlock> blocks) : blocks_(blocks) {
        for (unsigned i = 0; i < blocks.size(); ++i) blocks[i].next = i + 1;
        free_ = blocks.empty() ? kEnd : 0;
        if (!blocks.empty()) blocks.back().next = kEnd;
        available_ = static_cast<unsigned>(blocks.size());
    }
    static unsigned blocksFor(unsigned bytes) { return (bytes + kPoseBlockDataBytes - 1) / kPoseBlockDataBytes; }
    bool canStore(unsigned bytes) const { return blocksFor(bytes) <= available_; }
    unsigned store(std::span<const std::byte> input, unsigned parent = UINT32_MAX, unsigned parentBytes = 0) {
        if (input.empty() || input.size() > kPosePayloadMaxBytes ||
            !canStore(static_cast<unsigned>(input.size()))) return kEnd;
        const auto first = free_;
        blocks_[first].refs = 1;
        blocks_[first].parent = parent;
        blocks_[first].parentBytes = parentBytes;
        if (parentBytes) retain(parent);
        unsigned offset = 0, last = kEnd;
        while (offset < input.size()) {
            auto& block = blocks_[free_];
            last = free_;
            free_ = block.next;
            const auto count = std::min<unsigned>(kPoseBlockDataBytes, static_cast<unsigned>(input.size()) - offset);
            std::memcpy(block.bytes, input.data() + offset, count);
            offset += count;
            --available_;
        }
        blocks_[last].next = kEnd;
        return first;
    }
    void release(unsigned first, unsigned bytes) {
        while (bytes) {
            if (--blocks_[first].refs) return;
            const auto parent = blocks_[first].parent, parentBytes = blocks_[first].parentBytes;
            unsigned remaining = blocksFor(bytes);
            while (remaining--) {
                auto& block = blocks_[first];
                const auto next = block.next;
                block.next = free_;
                free_ = first;
                first = next;
                ++available_;
            }
            first = parent; bytes = parentBytes;
        }
    }
    void retain(unsigned first) { ++blocks_[first].refs; }
    bool parent(unsigned first, unsigned& parent, unsigned& bytes) const {
        if (first >= blocks_.size()) return false;
        parent = blocks_[first].parent; bytes = blocks_[first].parentBytes;
        return !bytes || parent < blocks_.size();
    }
    bool load(unsigned first, unsigned bytes, std::span<std::byte> output) const {
        if (!bytes || bytes > output.size()) return false;
        unsigned offset = 0;
        while (offset < bytes) {
            if (first >= blocks_.size()) return false;
            const auto& block = blocks_[first];
            const auto count = std::min(kPoseBlockDataBytes, bytes - offset);
            std::memcpy(output.data() + offset, block.bytes, count);
            offset += count;
            first = block.next;
        }
        return first == kEnd;
    }
private:
    static constexpr unsigned kEnd = UINT32_MAX;
    std::span<PosePayloadBlock> blocks_;
    unsigned free_ = kEnd, available_ = 0;
};

struct PoseDecodedFrame {
    std::atomic<bool> busy{false};
    RecordedPoseFrame frame{};
    std::array<std::byte, kPosePayloadMaxBytes> bytes{};
    PoseDecompressor decompressor;
    std::array<std::byte, kPosePayloadMaxBytes> raw{};
};
inline unsigned encodePosePayload(const PoseFrameInput& input, std::span<std::byte> output) {
    const auto& h = input.header;
    if (h.modelCount > kPoseModelLimit || h.boneCount > kPoseBoneLimit || !input.models || !input.bones) return 0;
    const unsigned modelBytes = h.modelCount * sizeof(RecordedModelPose);
    const unsigned boneBytes = h.boneCount * sizeof(RecordedBoneMatrix);
    const unsigned total = 4 + sizeof(RecordedVisibility) + modelBytes + boneBytes;
    if (total > output.size()) return 0;
    auto* cursor = output.data();
    auto append = [&](const void* p, unsigned n) { std::memcpy(cursor, p, n); cursor += n; };
    const std::uint32_t encoding = 0;
    append(&encoding, 4);
    append(&input.visible, sizeof(input.visible));
    append(input.models, modelBytes);
    append(input.bones, boneBytes);
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
    std::uint32_t encoding = 0;
    if (!take(&encoding, 4) || encoding || !take(&frame.visible, sizeof(frame.visible)) ||
        !take(frame.models, h.modelCount * sizeof(RecordedModelPose))) return false;
    if (!take(frame.bones, h.boneCount * sizeof(RecordedBoneMatrix))) return false;
    return offset == input.size();
}
}

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
        const auto bytes = encodePosePayload(input, raw_);
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
}

namespace self_recall::pure {

struct PoseHistorySlot {
    std::atomic<std::uint32_t> claims{0};
    std::uint32_t reserved[3]{};
    PoseFrameHeader header{};
    std::uint32_t firstBlock = 0, payloadBytes = 0;
};

static_assert(sizeof(PoseHistorySlot) * kHistoryCapacity <= kPoseHistoryByteLimit);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

enum class PoseRecordStatus : std::uint8_t {
    Recorded,
    InvalidInput,
    DuplicateFrame,
    TimeWentBackwards,
    ReaderBusy,
    SequenceExhausted,
    StorageFull,
};

struct PoseRecordReport {
    PoseRecordStatus status = PoseRecordStatus::InvalidInput;
    PoseFrameKey key{};
};

class PoseHistory;

class PoseReadLease {
public:
    PoseReadLease() = default;
    ~PoseReadLease() { release(); }
    PoseReadLease(const PoseReadLease&) = delete;
    PoseReadLease& operator=(const PoseReadLease&) = delete;
    PoseReadLease(PoseReadLease&& other) noexcept
        : slot_(std::exchange(other.slot_, nullptr)), decoded_(std::exchange(other.decoded_, nullptr)) {}
    PoseReadLease& operator=(PoseReadLease&& other) noexcept {
        if (this != &other) {
            release();
            slot_ = std::exchange(other.slot_, nullptr);
            decoded_ = std::exchange(other.decoded_, nullptr);
        }
        return *this;
    }

    explicit operator bool() const { return slot_ != nullptr; }
    const RecordedPoseFrame* get() const { return decoded_ ? &decoded_->frame : nullptr; }
    void release() {
        if (slot_) {
            slot_->claims.fetch_sub(1, std::memory_order_release);
            slot_ = nullptr;
            decoded_->busy.store(false, std::memory_order_release);
            decoded_ = nullptr;
        }
    }

private:
    friend class PoseHistory;
    PoseReadLease(PoseHistorySlot* slot, PoseDecodedFrame* decoded) : slot_(slot), decoded_(decoded) {}
    PoseHistorySlot* slot_ = nullptr;
    PoseDecodedFrame* decoded_ = nullptr;
};

class PoseHistory {
public:
    PoseHistory(PoseHistorySlot* slots, std::uint32_t capacity, std::span<PosePayloadBlock> blocks)
        : slots_(slots), capacity_(slots && capacity <= kHistoryCapacity ? capacity : 0), payload_(blocks) {}

    PoseHistory(const PoseHistory&) = delete;
    PoseHistory& operator=(const PoseHistory&) = delete;

    std::uint32_t count() const { return count_.load(std::memory_order_acquire); }
    std::uint32_t generation() const {
        return generation_.load(std::memory_order_acquire);
    }

    void clear() {
        chain_.reset(payload_);
        latest_.store(kNoSlot, std::memory_order_release);
        count_.store(0, std::memory_order_release);
        oldestSerial_.store(1, std::memory_order_release);
        const auto old = generation_.load(std::memory_order_relaxed);
        generation_.store(old == 0 || old == UINT32_MAX ? 0 : old + 1, std::memory_order_release);
        head_ = 0;
        nextSerial_ = 1;
        havePrevious_ = false;
        reclaimExpired();
    }

    PoseRecordReport record(const PoseFrameInput& input) {
        const auto& h = input.header;
        if (!capacity_ || !input.models || !input.bones || !h.modelCount ||
            !h.boneCount || h.modelCount > kPoseModelLimit || h.boneCount > kPoseBoneLimit ||
            h.materialCount > kPoseMaterialLimit || h.bodyModelCount > h.modelCount ||
            !h.worldGeneration || !h.modelGeneration || !finitePose(h.route.pose))
            return {PoseRecordStatus::InvalidInput, {}};

        std::uint32_t nextBone = 0;
        std::uint32_t nextMaterial = 0;
        for (std::uint16_t i = 0; i < h.modelCount; ++i) {
            const auto& model = input.models[i];
            const auto& identity = model.identity;
            if (!identity.unit || !identity.skeleton || !identity.resource ||
                !identity.boneCount || identity.firstBone != nextBone || model.originRelative > 1 ||
                identity.firstMaterial != nextMaterial || model.queueAdmission > 1)
                return {PoseRecordStatus::InvalidInput, {}};
            for (std::uint16_t j = 0; j < i; ++j)
                if (input.models[j].identity.unit == identity.unit)
                    return {PoseRecordStatus::InvalidInput, {}};
            nextBone += identity.boneCount;
            nextMaterial += identity.materialCount;
            if (nextBone > h.boneCount || nextMaterial > h.materialCount)
                return {PoseRecordStatus::InvalidInput, {}};
        }
        if (nextBone != h.boneCount || nextMaterial != h.materialCount)
            return {PoseRecordStatus::InvalidInput, {}};

        if (havePrevious_ && (h.worldGeneration != previousWorld_ ||
                              h.modelGeneration != previousModels_ || !sameModelSet(input)))
            clear();
        const auto currentGeneration = generation();
        if (!currentGeneration || nextSerial_ == UINT64_MAX)
            return {PoseRecordStatus::SequenceExhausted, {}};
        if (havePrevious_) {
            if (h.frameEpoch == previousEpoch_)
                return {PoseRecordStatus::DuplicateFrame, {}};
            if (h.frameEpoch < previousEpoch_ || h.elapsedNanoseconds <= previousTime_)
                return {PoseRecordStatus::TimeWentBackwards, {}};
        }
        trimBefore(h.elapsedNanoseconds > kRecallWindowNanoseconds ?
                   h.elapsedNanoseconds - kRecallWindowNanoseconds : 0, false);
        auto& slot = slots_[head_];
        std::uint32_t expected = 0;
        if (!slot.claims.compare_exchange_strong(expected, kWriter,
                                                std::memory_order_acquire,
                                                std::memory_order_relaxed))
            return {PoseRecordStatus::ReaderBusy, {}};

        unsigned parent = UINT32_MAX, parentBytes = 0;
        const auto bytes = chain_.prepare(input, scratch_, parent, parentBytes);
        if (bytes && !payload_.canStore(bytes)) reclaimExpired();
        if (!bytes || !payload_.canStore(bytes)) {
            slot.claims.store(0, std::memory_order_release);
            return {PoseRecordStatus::StorageFull, {}};
        }
        payload_.release(slot.firstBlock, slot.payloadBytes);
        slot.firstBlock = payload_.store({scratch_.data(), bytes}, parent, parentBytes);
        slot.payloadBytes = bytes;
        chain_.commit(payload_, slot.firstBlock, bytes);
        const PoseFrameKey key{nextSerial_++, currentGeneration, head_};
        slot.header = h;
        slot.header.key = key;
        slot.claims.store(0, std::memory_order_release);

        previousEpoch_ = h.frameEpoch;
        previousTime_ = h.elapsedNanoseconds;
        previousWorld_ = h.worldGeneration;
        previousModels_ = h.modelGeneration;
        previousModelCount_ = h.modelCount;
        previousBodyModelCount_ = h.bodyModelCount;
        for (std::uint16_t i = 0; i < h.modelCount; ++i)
            previousIdentities_[i] = input.models[i].identity;
        havePrevious_ = true;
        const auto oldCount = count_.load(std::memory_order_relaxed);
        const auto newCount = oldCount < capacity_ ? oldCount + 1 : capacity_;
        oldestSerial_.store(key.serial - newCount + 1, std::memory_order_release);
        count_.store(newCount, std::memory_order_release);
        latest_.store(head_, std::memory_order_release);
        head_ = (head_ + 1) % capacity_;
        return {PoseRecordStatus::Recorded, key};
    }

    PoseReadLease acquire(PoseFrameKey key) const {
        if (!key || key.slot >= capacity_ || key.generation != generation() ||
            key.serial < oldestSerial_.load(std::memory_order_acquire)) return {};
        auto lease = claim(key.slot);
        if (!lease || lease.get()->header.key != key || key.generation != generation() ||
            key.serial < oldestSerial_.load(std::memory_order_acquire))
            return {};
        return lease;
    }

    bool copyHeader(PoseFrameKey key, PoseFrameHeader& out) const {
        if (!key || key.slot >= capacity_ || key.generation != generation() ||
            key.serial < oldestSerial_.load(std::memory_order_acquire)) return false;
        auto* slot = slots_ + key.slot;
        auto readers = slot->claims.load(std::memory_order_relaxed);
        while (readers < kWriter - 1) {
            if (!slot->claims.compare_exchange_weak(readers, readers + 1,
                    std::memory_order_acquire, std::memory_order_relaxed)) continue;
            const bool valid = slot->header.key == key && key.generation == generation() &&
                key.serial >= oldestSerial_.load(std::memory_order_acquire);
            if (valid) out = slot->header;
            slot->claims.fetch_sub(1, std::memory_order_release);
            return valid;
        }
        return false;
    }

    bool copyHeaderBefore(PoseFrameKey anchor, std::uint32_t framesBack, PoseFrameHeader& out) const {
        PoseFrameHeader latest;
        if (framesBack >= capacity_ || anchor.serial <= framesBack || !copyHeader(anchor, latest)) return false;
        if (!framesBack) { out = latest; return true; }
        return copyHeader({anchor.serial - framesBack, anchor.generation,
                           (anchor.slot + capacity_ - framesBack) % capacity_}, out);
    }

    bool contains(PoseFrameKey key) const {
        PoseFrameHeader header;
        return copyHeader(key, header);
    }
    bool canReleaseAppearance(PoseFrameKey key) const {
        if (!key || key.slot >= capacity_) return true;
        if (key.generation == generation() &&
            key.serial >= oldestSerial_.load(std::memory_order_acquire)) return false;
        const auto& slot = slots_[key.slot];
        return slot.claims.load(std::memory_order_acquire) == 0;
    }

    void trimToWindow(std::uint64_t windowNanoseconds) {
        if (!havePrevious_) return;
        const auto cutoff = previousTime_ > windowNanoseconds
            ? previousTime_ - windowNanoseconds : 0;
        trimBefore(cutoff, true);
    }

private:
    void trimBefore(std::uint64_t cutoff, bool keepLatest) {
        auto count = count_.load(std::memory_order_relaxed);
        if (!count) return;
        auto oldest = (head_ + capacity_ - count) % capacity_;
        const auto firstExpired = oldest;
        unsigned expired = 0;
        while (count > unsigned(keepLatest) && slots_[oldest].header.elapsedNanoseconds < cutoff) {
            --count;
            ++expired;
            oldest = (oldest + 1) % capacity_;
        }
        oldestSerial_.store(nextSerial_ - count, std::memory_order_release);
        count_.store(count, std::memory_order_release);
        for (unsigned i = 0; i < expired; ++i) reclaim((firstExpired + i) % capacity_);
    }

public:
    PoseReadLease newest(std::uint32_t framesBack = 0) const {
        const auto index = latest_.load(std::memory_order_acquire);
        if (index == kNoSlot || framesBack >= count()) return {};
        auto latest = claim(index);
        if (!latest) return {};
        const auto key = latest.get()->header.key;
        if (key.generation != generation() || key.serial <= framesBack) return {};
        if (!framesBack) return latest;
        return acquire({key.serial - framesBack, key.generation,
                        (index + capacity_ - framesBack) % capacity_});
    }

    PoseReadLease before(PoseFrameKey anchor, std::uint32_t framesBack) const {
        PoseFrameHeader header;
        if (!copyHeaderBefore(anchor, framesBack, header)) return {};
        return acquire(header.key);
    }

private:
    bool sameModelSet(const PoseFrameInput& input) const {
        const auto body = input.header.bodyModelCount;
        if (body != previousBodyModelCount_ ||
            (!body && input.header.modelCount != previousModelCount_)) return false;
        const auto stableCount = body ? body : previousModelCount_;
        for (std::uint16_t i = 0; i < stableCount; ++i) {
            const auto& a = previousIdentities_[i];
            const auto& b = input.models[i].identity;
            if (a.unit != b.unit || a.skeleton != b.skeleton || a.resource != b.resource ||
                a.firstBone != b.firstBone || a.boneCount != b.boneCount ||
                a.firstMaterial != b.firstMaterial || a.materialCount != b.materialCount) return false;
        }
        return true;
    }

    PoseReadLease claim(std::uint32_t index) const {
        if (index >= capacity_) return {};
        auto* slot = slots_ + index;
        auto readers = slot->claims.load(std::memory_order_relaxed);
        while (readers < kWriter - 1) {
            if (slot->claims.compare_exchange_weak(readers, readers + 1,
                                                  std::memory_order_acquire,
                                                  std::memory_order_relaxed)) {
                for (auto& decoded : decoded_) {
                    bool available = false;
                    if (!decoded.busy.compare_exchange_strong(available, true, std::memory_order_acquire)) continue;
                    decoded.frame.header = slot->header;
                    if (slot->payloadBytes && decodePoseChain(payload_, slot->firstBlock, slot->payloadBytes, decoded, slot->header))
                        return PoseReadLease(slot, &decoded);
                    decoded.busy.store(false, std::memory_order_release);
                    break;
                }
                slot->claims.fetch_sub(1, std::memory_order_release);
                return {};
            }
        }
        return {};
    }

    void reclaim(unsigned index) {
        auto& slot = slots_[index];
        std::uint32_t expected = 0;
        if (!slot.claims.compare_exchange_strong(expected, kWriter, std::memory_order_acquire)) return;
        if (slot.header.key.generation != generation() ||
            slot.header.key.serial < oldestSerial_.load(std::memory_order_acquire)) {
            payload_.release(slot.firstBlock, slot.payloadBytes);
            slot.firstBlock = slot.payloadBytes = 0;
        }
        slot.claims.store(0, std::memory_order_release);
    }
    void reclaimExpired() {
        for (unsigned i = 0; i < capacity_; ++i) reclaim(i);
    }

    static constexpr std::uint32_t kWriter = 1u << 31;
    static constexpr std::uint32_t kNoSlot = UINT32_MAX;
    PoseHistorySlot* slots_;
    std::uint32_t capacity_;
    std::atomic<std::uint32_t> count_{0};
    std::atomic<std::uint32_t> latest_{kNoSlot};
    std::atomic<std::uint64_t> oldestSerial_{1};
    std::atomic<std::uint32_t> generation_{1};
    std::uint32_t head_ = 0;
    std::uint64_t nextSerial_ = 1;
    std::uint64_t previousEpoch_ = 0;
    std::uint64_t previousTime_ = 0;
    std::uint32_t previousWorld_ = 0;
    std::uint32_t previousModels_ = 0;
    RecordedModelIdentity previousIdentities_[kPoseModelLimit]{};
    std::uint16_t previousModelCount_ = 0;
    std::uint8_t previousBodyModelCount_ = 0;
    bool havePrevious_ = false;
    PosePayloadStore payload_;
    PoseChainEncoder chain_;
    std::array<std::byte, kPosePayloadMaxBytes> scratch_{};
    mutable std::array<PoseDecodedFrame, kPoseReadBufferCount> decoded_{};
};

}
