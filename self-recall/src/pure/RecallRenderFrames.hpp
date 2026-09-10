#pragma once

#include <atomic>
#include <span>
#include <utility>
#include "RecallPoseTransport.hpp"
#include "RecallPosePresentation.hpp"

namespace self_recall::pure {

struct RenderAnimationFrame {
    RecordedPoseFrame animation{};
    std::uint64_t epoch = 0;
    std::uint8_t buffers[kPoseModelLimit]{};
    float effectBones[kPoseBoneLimit][12]{};
    totk::core::WorldPosition rootOffset{};
};

inline bool prepareEffectBones(RenderAnimationFrame& out, const RecordedPoseFrame& source) {
    if (source.header.modelCount > kPoseModelLimit || source.header.boneCount > kPoseBoneLimit) return false;
    for (unsigned m = 0; m < source.header.modelCount; ++m) {
        const auto& model = source.models[m];
        if (model.identity.firstBone + model.identity.boneCount > source.header.boneCount) return false;
        for (unsigned j = 0; j < model.identity.boneCount; ++j) {
            const auto b = model.identity.firstBone + j;
            if (!boneToWorldMatrix(source.bones[b], model, out.effectBones[b])) return false;
        }
    }
    return true;
}

struct RenderWristFrame {
    PoseFrameKey key{};
    std::uint64_t epoch = 0;
    float matrix[12]{};
};

enum class RenderPrepareStatus : std::uint8_t {
    Ready, InvalidFrame, ModelMismatch, InvalidTransform, ReadersBusy,
};

enum class RenderModelStatus : std::uint8_t {
    Ready, ModelChanged, OriginChanged, BufferChanged, InvalidTransform, Busy,
};

inline RenderPrepareStatus prepareRenderAnimation(RenderAnimationFrame& out,
        const RecordedPoseFrame& recorded, std::span<const RecordedModelPose> current,
        std::uint64_t epoch) {
    out.epoch = 0;
    out.rootOffset = {};
    if (!epoch || !recorded.header.key || current.empty() ||
        current.size() != recorded.header.modelCount || current.size() > kPoseModelLimit ||
        recorded.header.boneCount > kPoseBoneLimit || recorded.header.materialCount > kPoseMaterialLimit)
        return RenderPrepareStatus::InvalidFrame;
    for (std::size_t i = 0; i < current.size(); ++i) {
        const auto& a = recorded.models[i].identity;
        const auto& b = current[i].identity;
        if (a.unit != b.unit || a.skeleton != b.skeleton || a.resource != b.resource ||
            a.boneCount != b.boneCount || a.materialCount != b.materialCount ||
            a.firstBone + a.boneCount > recorded.header.boneCount)
            return RenderPrepareStatus::ModelMismatch;
    }
    out.animation = recorded;
    if (!prepareEffectBones(out, recorded)) return RenderPrepareStatus::InvalidTransform;
    for (std::size_t i = 0; i < current.size(); ++i) {
        const auto& from = recorded.models[i];
        const auto& to = current[i];
        if (from.originRelative > 1 || to.originRelative > 1)
            return RenderPrepareStatus::InvalidTransform;
        for (std::size_t j = 0; j < from.identity.boneCount; ++j) {
            const auto bone = from.identity.firstBone + j;
            float matrix[12];
            if (!boneToWorldMatrix(recorded.bones[bone], from, matrix))
                return RenderPrepareStatus::InvalidTransform;
            if (!rebaseBoneForRender(recorded.bones[bone], from, to, out.animation.bones[bone]))
                return RenderPrepareStatus::InvalidTransform;
        }
        std::memcpy(out.animation.models[i].renderOrigin, to.renderOrigin, sizeof(to.renderOrigin));
        out.animation.models[i].originRelative = to.originRelative;
        out.buffers[i] = static_cast<std::uint8_t>((to.visibility >> 24) & 3u);
    }
    out.epoch = epoch;
    return RenderPrepareStatus::Ready;
}

class RenderFrameStore {
    static constexpr std::uint32_t kWriting = 1u << 31;
    struct Slot {
        std::atomic<std::uint32_t> readers{0};
        RenderAnimationFrame frame{};
        std::atomic<unsigned> prepared[kPoseModelLimit]{};
        std::atomic<bool> uploaded[kPoseModelLimit]{};
        std::atomic<bool> bounded[kPoseModelLimit]{};
    };
public:
    class Lease {
        friend class RenderFrameStore;
        Slot* slot_ = nullptr;
        std::uint16_t model_ = 0;
        Lease(Slot* slot, std::uint16_t model) : slot_(slot), model_(model) {}
    public:
        Lease() = default;
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept : slot_(std::exchange(other.slot_, nullptr)), model_(other.model_) {}
        Lease& operator=(Lease&& other) noexcept {
            if (this != &other) { release(); slot_ = std::exchange(other.slot_, nullptr); model_ = other.model_; }
            return *this;
        }
        ~Lease() { release(); }
        void release() {
            if (slot_) slot_->readers.fetch_sub(1, std::memory_order_release);
            slot_ = nullptr;
        }
        explicit operator bool() const { return slot_ != nullptr; }
        const RenderAnimationFrame* get() const { return slot_ ? &slot_->frame : nullptr; }
        std::uint16_t modelIndex() const { return model_; }
        void markUploaded() const {
            if (slot_ && slot_->prepared[model_].load(std::memory_order_acquire) == 5)
                slot_->uploaded[model_].store(true, std::memory_order_release);
        }
        bool uploaded(unsigned modelIndex) const {
            return slot_ && modelIndex < slot_->frame.animation.header.modelCount &&
                   slot_->uploaded[modelIndex].load(std::memory_order_acquire);
        }
        void markBounded() const {
            if (slot_) slot_->bounded[model_].store(true, std::memory_order_release);
        }
        bool bounded(unsigned modelIndex) const {
            return slot_ && modelIndex < slot_->frame.animation.header.modelCount &&
                slot_->bounded[modelIndex].load(std::memory_order_acquire);
        }
        bool copyWorldBone(unsigned bone, float out[12]) const {
            if (!slot_ || !out) return false;
            const auto& model = slot_->frame.animation.models[model_].identity;
            if (bone >= model.boneCount || model.firstBone + bone >= kPoseBoneLimit) return false;
            std::memcpy(out, slot_->frame.effectBones[model.firstBone + bone], 48);
            return true;
        }
        RenderModelStatus prepareBones(const RecordedModelPose& current) const {
            if (!slot_) return RenderModelStatus::ModelChanged;
            auto& animation = slot_->frame.animation;
            auto& model = animation.models[model_];
            const auto& a = model.identity;
            const auto& b = current.identity;
            if (a.unit != b.unit || a.skeleton != b.skeleton || a.resource != b.resource ||
                a.boneCount != b.boneCount || a.materialCount != b.materialCount)
                return RenderModelStatus::ModelChanged;
            auto& state = slot_->prepared[model_];
            unsigned expected = 0;
            if (!state.compare_exchange_strong(expected, 1, std::memory_order_acquire,
                                               std::memory_order_acquire)) {
                if (expected != 2 && expected != 5) return RenderModelStatus::Busy;
                if (!sameRenderSpace(model, current))
                    return RenderModelStatus::OriginChanged;
                return RenderModelStatus::Ready;
            }
            if (model.originRelative > 1 || current.originRelative > 1 ||
                a.firstBone + a.boneCount > animation.header.boneCount) {
                state.store(3, std::memory_order_release);
                return RenderModelStatus::InvalidTransform;
            }
            for (unsigned i = 0; i < a.boneCount; ++i) {
                auto& bone = animation.bones[a.firstBone + i];
                float matrix[12];
                if (!boneToWorldMatrix(bone, model, matrix) ||
                    !rebaseBoneForRender(bone, model, current, bone)) {
                    state.store(3, std::memory_order_release);
                    return RenderModelStatus::InvalidTransform;
                }
            }
            std::memcpy(model.renderOrigin, current.renderOrigin, sizeof(model.renderOrigin));
            model.originRelative = current.originRelative;
            state.store(2, std::memory_order_release);
            return RenderModelStatus::Ready;
        }
        RenderModelStatus prepareModel(const RecordedModelPose& current) const {
            const auto bones = prepareBones(current);
            if (bones != RenderModelStatus::Ready) return bones;
            auto& state = slot_->prepared[model_];
            unsigned expected = 2;
            const auto buffer = static_cast<std::uint8_t>((current.visibility >> 24) & 3u);
            if (state.compare_exchange_strong(expected, 4, std::memory_order_acquire,
                                              std::memory_order_acquire)) {
                slot_->frame.buffers[model_] = buffer;
                state.store(5, std::memory_order_release);
                return RenderModelStatus::Ready;
            }
            if (expected != 5) return RenderModelStatus::Busy;
            return slot_->frame.buffers[model_] == buffer
                 ? RenderModelStatus::Ready : RenderModelStatus::BufferChanged;
        }
        RenderModelStatus validateUploadedModel(const RecordedModelPose& current) const {
            if (!uploaded(model_)) return RenderModelStatus::Busy;
            const auto& a = slot_->frame.animation.models[model_].identity;
            const auto& b = current.identity;
            if (a.unit != b.unit || a.skeleton != b.skeleton || a.resource != b.resource ||
                a.boneCount != b.boneCount || a.materialCount != b.materialCount)
                return RenderModelStatus::ModelChanged;
            return slot_->frame.buffers[model_] == ((current.visibility >> 24) & 3u)
                 ? RenderModelStatus::Ready : RenderModelStatus::BufferChanged;
        }
    };
    struct Lookup { Lease lease; bool owned = false; };

    RenderPrepareStatus publish(const RecordedPoseFrame& recorded,
            std::span<const RecordedModelPose> current, std::uint64_t epoch) {
        auto& slot = slots_[next_];
        std::uint32_t expected = 0;
        if (!slot.readers.compare_exchange_strong(expected, kWriting,
                std::memory_order_acquire, std::memory_order_relaxed))
            return RenderPrepareStatus::ReadersBusy;
        const auto result = prepareRenderAnimation(slot.frame, recorded, current, epoch);
        for (auto& state : slot.prepared) state.store(5, std::memory_order_relaxed);
        for (auto& uploaded : slot.uploaded) uploaded.store(false, std::memory_order_relaxed);
        for (auto& bounded : slot.bounded) bounded.store(false, std::memory_order_relaxed);
        slot.readers.store(0, std::memory_order_release);
        if (result == RenderPrepareStatus::Ready) next_ = (next_ + 1) % 3;
        return result;
    }

    RenderPrepareStatus begin(const RecordedPoseFrame& recorded, std::uint64_t epoch,
                             const totk::core::WorldPosition& offset = {}) {
        auto& slot = slots_[next_];
        std::uint32_t expected = 0;
        if (!slot.readers.compare_exchange_strong(expected, kWriting,
                std::memory_order_acquire, std::memory_order_relaxed))
            return RenderPrepareStatus::ReadersBusy;
        slot.frame.epoch = 0;
        slot.frame.animation = recorded;
        const bool valid = epoch && recorded.header.key && recorded.header.modelCount &&
            recorded.header.modelCount <= kPoseModelLimit && recorded.header.boneCount <= kPoseBoneLimit &&
            recorded.header.materialCount <= kPoseMaterialLimit &&
            translateAnimation(slot.frame.animation, offset) && prepareEffectBones(slot.frame, slot.frame.animation);
        if (valid) {
            slot.frame.rootOffset = offset;
            for (auto& state : slot.prepared) state.store(0, std::memory_order_relaxed);
            for (auto& uploaded : slot.uploaded) uploaded.store(false, std::memory_order_relaxed);
            for (auto& bounded : slot.bounded) bounded.store(false, std::memory_order_relaxed);
            std::memset(slot.frame.buffers, 0xFF, sizeof(slot.frame.buffers));
            slot.frame.epoch = epoch;
            next_ = (next_ + 1) % 3;
        }
        slot.readers.store(0, std::memory_order_release);
        return valid ? RenderPrepareStatus::Ready : RenderPrepareStatus::InvalidFrame;
    }

    bool copyHeader(std::uint64_t epoch, std::uint32_t generation, PoseFrameHeader& out) {
        if (!epoch || !generation) return false;
        for (auto& slot : slots_) {
            auto readers = slot.readers.load(std::memory_order_relaxed);
            while (readers < kWriting - 1 && !slot.readers.compare_exchange_weak(
                    readers, readers + 1, std::memory_order_acquire, std::memory_order_relaxed)) {}
            if (readers >= kWriting - 1) continue;
            Lease candidate(&slot, 0);
            const auto& frame = slot.frame;
            if (frame.epoch != epoch || frame.animation.header.key.generation != generation) continue;
            out = frame.animation.header;
            return true;
        }
        return false;
    }

    bool copyWrist(std::uint64_t epoch, std::uint32_t generation, RenderWristFrame& out) {
        if (!epoch || !generation) return false;
        for (auto& slot : slots_) {
            auto readers = slot.readers.load(std::memory_order_relaxed);
            while (readers < kWriting - 1 && !slot.readers.compare_exchange_weak(
                    readers, readers + 1, std::memory_order_acquire, std::memory_order_relaxed)) {}
            if (readers >= kWriting - 1) continue;
            Lease candidate(&slot, 0);
            const auto& frame = slot.frame;
            const auto& header = frame.animation.header;
            if (frame.epoch != epoch || header.key.generation != generation || !header.haveWrist) continue;
            for (float value : header.wristMatrix) if (!std::isfinite(value)) return false;
            out.key = header.key;
            out.epoch = frame.epoch;
            std::memcpy(out.matrix, header.wristMatrix, sizeof(out.matrix));
            return true;
        }
        return false;
    }

    Lookup acquireEpoch(std::uintptr_t unit, std::uint64_t epoch, std::uint32_t generation) {
        Lookup out;
        for (auto& slot : slots_) {
            auto readers = slot.readers.load(std::memory_order_relaxed);
            while (readers < kWriting - 1 && !slot.readers.compare_exchange_weak(
                    readers, readers + 1, std::memory_order_acquire, std::memory_order_relaxed)) {}
            if (readers >= kWriting - 1) continue;
            Lease candidate(&slot, 0);
            const auto& frame = slot.frame;
            if (!frame.epoch || frame.animation.header.key.generation != generation) continue;
            for (std::uint16_t m = 0; m < frame.animation.header.modelCount; ++m) {
                if (frame.animation.models[m].identity.unit != unit) continue;
                out.owned = true;
                if (frame.epoch == epoch) {
                    candidate.model_ = m;
                    out.lease = std::move(candidate);
                    return out;
                }
                break;
            }
        }
        return out;
    }

    Lookup acquire(std::uintptr_t unit, unsigned buffer, std::uint32_t generation) {
        Lookup out;
        for (auto& slot : slots_) {
            auto readers = slot.readers.load(std::memory_order_relaxed);
            while (readers < kWriting - 1 && !slot.readers.compare_exchange_weak(
                    readers, readers + 1, std::memory_order_acquire, std::memory_order_relaxed)) {}
            if (readers >= kWriting - 1) continue;
            Lease candidate(&slot, 0);
            const auto& frame = slot.frame;
            if (!frame.epoch || frame.animation.header.key.generation != generation) continue;
            for (std::uint16_t m = 0; m < frame.animation.header.modelCount; ++m) {
                if (frame.animation.models[m].identity.unit != unit) continue;
                out.owned = true;
                if (slot.prepared[m].load(std::memory_order_acquire) != 5) break;
                if (frame.buffers[m] == buffer && (!out.lease || out.lease.get()->epoch < frame.epoch)) {
                    candidate.model_ = m;
                    out.lease = std::move(candidate);
                }
                break;
            }
        }
        return out;
    }
private:
    Slot slots_[3];
    std::size_t next_ = 0;
};

} // namespace self_recall::pure
