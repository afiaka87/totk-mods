#pragma once
#include "RecallPoseFrame.hpp"
#include "RecallPosePayload.hpp"
#include "RecallGameTime.hpp"

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

    const PosePayloadUsage& payloadUsage() const { return payload_.usage(); }
    std::uint64_t readFailures() const { return readFailures_.load(std::memory_order_relaxed); }
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

        // Expiry follows the incoming clock even when a previous allocation was refused.
        trimBefore(h.elapsedNanoseconds > kRecallWindowNanoseconds ?
                   h.elapsedNanoseconds - kRecallWindowNanoseconds : 0, false);
        auto& slot = slots_[head_];
        std::uint32_t expected = 0;
        if (!slot.claims.compare_exchange_strong(expected, kWriter,
                                                std::memory_order_acquire,
                                                std::memory_order_relaxed))
            return {PoseRecordStatus::ReaderBusy, {}};

        const auto bytes = encodePosePayload(input, scratch_);
        if (bytes && !payload_.canReplace(slot.payloadBytes, bytes)) reclaimExpired();
        if (!bytes || !payload_.canReplace(slot.payloadBytes, bytes)) {
            slot.claims.store(0, std::memory_order_release);
            ++storageFailures_;
            return {PoseRecordStatus::StorageFull, {}};
        }
        payload_.release(slot.firstBlock, slot.payloadBytes);
        slot.firstBlock = payload_.store({scratch_.data(), bytes});
        slot.payloadBytes = bytes;
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
                    if (slot->payloadBytes && payload_.load(slot->firstBlock, slot->payloadBytes, decoded.bytes) &&
                        decodePosePayload({decoded.bytes.data(), slot->payloadBytes}, decoded.frame))
                        return PoseReadLease(slot, &decoded);
                    decoded.busy.store(false, std::memory_order_release);
                    break;
                }
                slot->claims.fetch_sub(1, std::memory_order_release);
                readFailures_.fetch_add(1, std::memory_order_relaxed);
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
    std::array<std::byte, kPosePayloadMaxBytes> scratch_{};
    mutable std::array<PoseDecodedFrame, kPoseReadBufferCount> decoded_{};
    mutable std::atomic<std::uint64_t> readFailures_{0};
    std::uint64_t storageFailures_ = 0;
public:
    std::uint64_t storageFailures() const { return storageFailures_; }
};

}  // namespace self_recall::pure
