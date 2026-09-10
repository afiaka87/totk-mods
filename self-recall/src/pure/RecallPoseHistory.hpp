#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

#include "RecallHistory.hpp"

namespace self_recall::pure {

inline constexpr std::uint16_t kPoseModelLimit = 32;
inline constexpr std::uint16_t kPoseBoneLimit = 512;
inline constexpr std::uint16_t kPoseMaterialLimit = 512;
inline constexpr std::size_t kPoseHistoryByteLimit = 128u * 1024u * 1024u;

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
    std::uint8_t reserved[8]{}; // Existing equipment-effect mask; preserve verbatim.
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
    PoseFrameHeader header{};  // key is assigned by the history
    const RecordedModelPose* models = nullptr;
    const RecordedBoneMatrix* bones = nullptr;
    RecordedVisibility visible{};
};

struct PoseHistorySlot {
    std::atomic<std::uint32_t> claims{0};
    std::uint32_t reserved[3]{};
    RecordedPoseFrame frame{};
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
        : slot_(std::exchange(other.slot_, nullptr)) {}
    PoseReadLease& operator=(PoseReadLease&& other) noexcept {
        if (this != &other) {
            release();
            slot_ = std::exchange(other.slot_, nullptr);
        }
        return *this;
    }

    explicit operator bool() const { return slot_ != nullptr; }
    const RecordedPoseFrame* get() const { return slot_ ? &slot_->frame : nullptr; }
    void release() {
        if (slot_) {
            slot_->claims.fetch_sub(1, std::memory_order_release);
            slot_ = nullptr;
        }
    }

private:
    friend class PoseHistory;
    explicit PoseReadLease(PoseHistorySlot* slot) : slot_(slot) {}
    PoseHistorySlot* slot_ = nullptr;
};

class PoseHistory {
public:
    PoseHistory(PoseHistorySlot* slots, std::uint32_t capacity)
        : slots_(slots), capacity_(slots && capacity <= kHistoryCapacity ? capacity : 0) {}
    PoseHistory(const PoseHistory&) = delete;
    PoseHistory& operator=(const PoseHistory&) = delete;

    std::uint32_t count() const { return count_.load(std::memory_order_acquire); }
    std::uint32_t generation() const {
        return generation_.load(std::memory_order_acquire);
    }

    void clear() {
        latest_.store(kNoSlot, std::memory_order_release);
        count_.store(0, std::memory_order_release);
        oldestSerial_.store(1, std::memory_order_release);
        const auto old = generation_.load(std::memory_order_relaxed);
        generation_.store(old == 0 || old == UINT32_MAX ? 0 : old + 1, std::memory_order_release);
        head_ = 0;
        nextSerial_ = 1;
        havePrevious_ = false;
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

        auto& slot = slots_[head_];
        std::uint32_t expected = 0;
        if (!slot.claims.compare_exchange_strong(expected, kWriter,
                                                std::memory_order_acquire,
                                                std::memory_order_relaxed))
            return {PoseRecordStatus::ReaderBusy, {}};

        const PoseFrameKey key{nextSerial_++, currentGeneration, head_};
        slot.frame.header = h;
        slot.frame.header.key = key;
        std::memcpy(slot.frame.models, input.models, sizeof(*input.models) * h.modelCount);
        std::memcpy(slot.frame.bones, input.bones, sizeof(*input.bones) * h.boneCount);
        slot.frame.visible = input.visible;
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

    void trimToWindow(std::uint64_t windowNanoseconds) {
        if (!havePrevious_) return;
        const auto cutoff = previousTime_ > windowNanoseconds
            ? previousTime_ - windowNanoseconds : 0;
        auto count = count_.load(std::memory_order_relaxed);
        auto oldest = (head_ + capacity_ - count) % capacity_;
        while (count > 1 && slots_[oldest].frame.header.elapsedNanoseconds < cutoff) {
            --count;
            oldest = (oldest + 1) % capacity_;
        }
        oldestSerial_.store(nextSerial_ - count, std::memory_order_release);
        count_.store(count, std::memory_order_release);
    }

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
        auto pinned = acquire(anchor);
        if (!pinned || framesBack >= capacity_ || anchor.serial <= framesBack) return {};
        if (!framesBack) return pinned;
        return acquire({anchor.serial - framesBack, anchor.generation,
                        (anchor.slot + capacity_ - framesBack) % capacity_});
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
                                                  std::memory_order_relaxed))
                return PoseReadLease(slot);
        }
        return {};
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
};

}  // namespace self_recall::pure
