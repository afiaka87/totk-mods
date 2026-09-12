#pragma once

#include <array>
#include <cstdint>
#include <limits>

namespace self_recall::pure {

class RecallGpuLifetime {
public:
    static constexpr unsigned kSlots = 3;
    static constexpr unsigned kPools = 9;
    static constexpr unsigned kRecorders = 8;
    static constexpr unsigned kFrames = 4;
    using Address = std::uintptr_t;

    bool configurePool(Address id, Address objects, std::uint32_t count, std::uint32_t stride) {
        if (failed_ || !id || !objects || !count || !stride ||
            count > (std::numeric_limits<Address>::max() - objects) / stride) return fail();
        auto* pool = findPool(id);
        if (pool) {
            if (pool->objects == objects && pool->count == count && pool->stride == stride) return true;
            if (pool->mask || recordingPool(id)) return fail();
        } else {
            for (auto& item : pools_) if (!item.id) { pool = &item; break; }
            if (!pool) return fail();
        }
        const auto end = objects + Address{count} * stride;
        for (const auto& item : pools_) {
            if (&item != pool && item.id && objects < item.objects + Address{item.count} * item.stride &&
                item.objects < end) return fail();
        }
        *pool = {id, objects, count, stride, 0};
        return true;
    }

    bool beginList(Address commandBuffer, Address poolId) {
        if (failed_ || !commandBuffer || !findPool(poolId) || recording(commandBuffer)) return fail();
        for (auto& recorder : recorders_) if (!recorder.commandBuffer) {
            recorder = {commandBuffer, poolId};
            return true;
        }
        return fail();
    }
    bool endList(Address commandBuffer) {
        if (failed_) return false;
        for (auto& recorder : recorders_) if (recorder.commandBuffer == commandBuffer && commandBuffer) {
            recorder = {};
            return true;
        }
        return fail();
    }
    bool clearPool(Address poolId) {
        auto* pool = findPool(poolId);
        if (failed_ || !pool || recordingPool(poolId)) return fail();
        pool->mask = 0;
        return true;
    }

    bool beginFrame(Address commandBuffer) {
        if (failed_ || !commandBuffer || recording(commandBuffer) || nextFrame_ == UINT64_MAX) return fail();
        Frame* available = nullptr;
        for (auto& frame : frames_) {
            if (frame.phase == Phase::Empty) { available = &frame; break; }
            if (frame.phase == Phase::Submitted && frame.submission <= completed_ &&
                (!available || frame.serial < available->serial)) available = &frame;
        }
        if (!available) return fail();
        *available = {commandBuffer, 0, ++nextFrame_, 0, 0, Phase::Recording};
        return true;
    }

    bool bindSlot(Address commandBuffer, unsigned slot) {
        if (failed_ || slot >= kSlots || !commandBuffer) return fail();
        const auto bit = static_cast<std::uint8_t>(1u << slot);
        for (const auto& recorder : recorders_) if (recorder.commandBuffer == commandBuffer) {
            auto* pool = findPool(recorder.pool);
            if (!pool) return fail();
            pool->mask |= bit;
            return true;
        }
        if (auto* frame = recordingFrame(commandBuffer)) { frame->mask |= bit; return true; }
        return fail();
    }

    bool callList(Address commandBuffer, Address listObject) {
        if (failed_ || !commandBuffer || !listObject) return fail();
        for (const auto& pool : pools_) {
            if (pool.id && listObject >= pool.objects &&
                listObject < pool.objects + Address{pool.count} * pool.stride) {
                if ((listObject - pool.objects) % pool.stride) return fail();
                if (!pool.mask) return true;
                auto* frame = recordingFrame(commandBuffer);
                Pool* destination = nullptr;
                for (const auto& recorder : recorders_)
                    if (recorder.commandBuffer == commandBuffer) destination = findPool(recorder.pool);
                if (!frame && !destination) return fail();
                if (frame) frame->mask |= pool.mask;
                else destination->mask |= pool.mask;
                return true;
            }
        }
        return true;
    }

    bool copyListObject(Address sourceObject, Address destinationObject) {
        const auto mask = listMask(sourceObject);
        if (failed_) return false;
        for (auto& pool : pools_) if (contains(pool, destinationObject)) {
            if ((destinationObject - pool.objects) % pool.stride) return fail();
            pool.mask |= mask;
            return true;
        }
        return mask ? fail() : true;
    }

    std::uint8_t listMask(Address object) {
        if (!object) { fail(); return 0; }
        for (const auto& pool : pools_) if (contains(pool, object)) {
            if ((object - pool.objects) % pool.stride) { fail(); return 0; }
            return pool.mask;
        }
        return 0;
    }

    std::uint8_t beginDirect(Address object) {
        const auto mask = listMask(object);
        if (failed_ || !mask) return 0;
        if (nextDirect_ == UINT64_MAX || activeDirect_ == UINT32_MAX) { fail(); return 0; }
        ++activeDirect_;
        ++nextDirect_;
        for (unsigned slot = 0; slot < kSlots; ++slot)
            if (mask & (1u << slot)) pendingDirect_[slot] = nextDirect_;
        return mask;
    }
    bool endDirect(std::uint8_t mask) {
        if (failed_) return false;
        if (!mask) return true;
        if (!activeDirect_ || mask >= (1u << kSlots)) return fail();
        --activeDirect_;
        return true;
    }
    std::uint64_t captureDirectFence() const {
        return !failed_ && !activeDirect_ ? nextDirect_ : 0;
    }

    bool sealFrame(Address commandBuffer, std::uint64_t commandHandle) {
        auto* frame = recordingFrame(commandBuffer);
        if (failed_ || !frame || !commandHandle) return fail();
        for (auto& prior : frames_) if (&prior != frame && prior.handle == commandHandle) {
            if (prior.phase != Phase::Submitted || prior.submission > completed_) return fail();
            prior = {};
        }
        frame->handle = commandHandle;
        frame->phase = Phase::Sealed;
        return true;
    }

    bool beginSubmission(std::uint64_t commandHandle) {
        if (failed_ || !commandHandle) return fail();
        for (auto& frame : frames_) if (frame.handle == commandHandle &&
                (frame.phase == Phase::Sealed || frame.phase == Phase::Submitted)) {
            frame.phase = Phase::Sealed;
            return true;
        }
        return fail();
    }

    std::uint64_t submittedAndFenced(std::uint64_t commandHandle, Address queue, Address sync,
                                     std::uint64_t directTicket = 0) {
        if (failed_ || !queue || !sync || !commandHandle || nextSubmission_ == UINT64_MAX ||
            directTicket > nextDirect_ ||
            (queue_ && (queue_ != queue || sync_ != sync))) { fail(); return 0; }
        for (auto& frame : frames_) if (frame.handle == commandHandle && frame.phase == Phase::Sealed) {
            queue_ = queue;
            sync_ = sync;
            frame.submission = ++nextSubmission_;
            frame.phase = Phase::Submitted;
            for (unsigned slot = 0; slot < kSlots; ++slot) {
                const bool directCovered = pendingDirect_[slot] && pendingDirect_[slot] <= directTicket;
                if ((frame.mask & (1u << slot)) || directCovered) submitted_[slot] = frame.submission;
                if (directCovered) pendingDirect_[slot] = 0;
            }
            return frame.submission;
        }
        fail();
        return 0;
    }
    std::uint64_t captureWait(Address queue, Address sync) const {
        return !failed_ && queue == queue_ && sync == sync_ ? nextSubmission_ : 0;
    }
    bool completeWait(std::uint64_t ticket, unsigned nativeResult) {
        if (failed_ || !ticket || ticket > nextSubmission_ || nativeResult > 1) return false;
        if (ticket > completed_) completed_ = ticket;
        return true;
    }
    bool canWrite(unsigned slot) const {
        if (failed_ || slot >= kSlots || submitted_[slot] > completed_ || pendingDirect_[slot]) return false;
        const auto bit = 1u << slot;
        for (const auto& pool : pools_) if (pool.mask & bit) return false;
        for (const auto& frame : frames_)
            if ((frame.phase == Phase::Recording || frame.phase == Phase::Sealed) && (frame.mask & bit)) return false;
        return true;
    }
    bool failed() const { return failed_; }
    void freeze() { failed_ = true; }
    bool hasPool(Address id) const {
        if (id) for (const auto& pool : pools_) if (pool.id == id) return true;
        return false;
    }
    bool hasRecorder(Address commandBuffer) const {
        if (commandBuffer) for (const auto& recorder : recorders_)
            if (recorder.commandBuffer == commandBuffer) return true;
        return false;
    }
    bool hasRecordingFrame(Address commandBuffer) { return recordingFrame(commandBuffer) != nullptr; }

private:
    enum class Phase : std::uint8_t { Empty, Recording, Sealed, Submitted };
    struct Pool {
        Address id = 0, objects = 0;
        std::uint32_t count = 0, stride = 0;
        std::uint8_t mask = 0;
    };
    struct Recorder { Address commandBuffer = 0, pool = 0; };
    struct Frame {
        Address commandBuffer = 0;
        std::uint64_t handle = 0, serial = 0, submission = 0;
        std::uint8_t mask = 0;
        Phase phase = Phase::Empty;
    };
    static bool contains(const Pool& pool, Address object) {
        return pool.id && object >= pool.objects &&
            object < pool.objects + Address{pool.count} * pool.stride;
    }
    Pool* findPool(Address id) {
        if (id) for (auto& pool : pools_) if (pool.id == id) return &pool;
        return nullptr;
    }
    Frame* recordingFrame(Address commandBuffer) {
        if (commandBuffer) for (auto& frame : frames_)
            if (frame.phase == Phase::Recording && frame.commandBuffer == commandBuffer) return &frame;
        return nullptr;
    }
    bool recording(Address commandBuffer) {
        if (recordingFrame(commandBuffer)) return true;
        for (const auto& recorder : recorders_) if (recorder.commandBuffer == commandBuffer) return true;
        return false;
    }
    bool recordingPool(Address pool) const {
        for (const auto& recorder : recorders_) if (recorder.pool == pool) return true;
        return false;
    }
    bool fail() { failed_ = true; return false; }
    std::array<Pool, kPools> pools_{};
    std::array<Recorder, kRecorders> recorders_{};
    std::array<Frame, kFrames> frames_{};
    std::array<std::uint64_t, kSlots> submitted_{};
    std::array<std::uint64_t, kSlots> pendingDirect_{};
    std::uint64_t nextDirect_ = 0;
    std::uint32_t activeDirect_ = 0;
    Address queue_ = 0, sync_ = 0;
    std::uint64_t nextFrame_ = 0, nextSubmission_ = 0, completed_ = 0;
    bool failed_ = false;
};

static_assert(sizeof(RecallGpuLifetime) <= 768);

}

#include <atomic>
#include <span>
#include <utility>
#include "RecallPlayback.hpp"

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
    Ready, InvalidFrame, ModelMismatch, ReadersBusy = 4,
};

enum class RenderModelStatus : std::uint8_t {
    Ready, ModelChanged, OriginChanged, BufferChanged, InvalidTransform, Busy,
};

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

private:
    Slot slots_[3];
    std::size_t next_ = 0;
};

}

#include <bit>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace self_recall::pure {

struct CompactEffectHandle {
    std::uint8_t type = 0xFF;
    std::uint8_t padding = 0;
    std::int16_t poolIndex = -1;
    std::uint32_t eventId = 0;
};
static_assert(sizeof(CompactEffectHandle) == 8);

struct WristEffectBinding {
    CompactEffectHandle handle{};
    std::uintptr_t event = 0;
};

struct WristEffectOwner {
    std::uint64_t serial = 0;
    std::uint32_t historyGeneration = 0;
    explicit operator bool() const { return serial && historyGeneration; }
};

class WristEffectOwners {
public:
    std::uint64_t publish(std::uint32_t historyGeneration,
                          std::span<const WristEffectBinding> bindings) {
        clear();
        if (!historyGeneration || bindings.size() > 2 || next_ == UINT64_MAX) return 0;
        for (unsigned i = 0; i < 2; ++i) {
            const auto binding = i < bindings.size() ? bindings[i] : WristEffectBinding{};
            events_[i].store(binding.event);
            handles_[i].store(std::bit_cast<std::uint64_t>(binding.handle));
        }
        history_.store(historyGeneration);
        serial_.store(++next_);
        return next_;
    }
    void clear() { serial_.store(0); }
    bool current(std::uint64_t serial) const { return serial && serial_.load() == serial; }
    WristEffectOwner match(std::uintptr_t event, std::uint32_t eventId) const {
        const auto serial = serial_.load();
        if (!serial || !event) return {};
        const auto history = history_.load();
        bool matched = false;
        for (unsigned i = 0; i < 2; ++i) {
            const auto handle = std::bit_cast<CompactEffectHandle>(handles_[i].load());
            matched |= events_[i].load() == event && handle.type < 0x80 &&
                       handle.poolIndex >= 0 && handle.eventId == eventId;
        }
        return matched && current(serial) ? WristEffectOwner{serial, history} : WristEffectOwner{};
    }
private:
    std::atomic<std::uint64_t> serial_{0};
    std::atomic<std::uint32_t> history_{0};
    std::atomic<std::uintptr_t> events_[2]{};
    std::atomic<std::uint64_t> handles_[2]{};
    std::uint64_t next_ = 0;
};

struct WristMatrixDescriptor { void* object; std::uint64_t serial; };
static_assert(sizeof(WristMatrixDescriptor) == 16);

class WristMatrixProvider {
    struct Vtable {
        bool (*valid)(const WristMatrixProvider*, const void*);
        void (*copy)(WristMatrixProvider*, void*, const void*);
    };
    static bool valid(const WristMatrixProvider* self, const void* data) {
        std::uint64_t serial = 0;
        if (data) std::memcpy(&serial, data, sizeof(serial));
        return self && self->finite_ && serial == self->serial_ && self->owners_->current(serial);
    }
    static void copy(WristMatrixProvider* self, void* out, const void* data) {
        if (!out || !valid(self, data)) return;
        std::memcpy(out, self->matrix_, sizeof(self->matrix_));
        self->copied_ = true;
    }
    inline static const Vtable kVtable{valid, copy};
    const Vtable* vtable_ = &kVtable;
    const WristEffectOwners* owners_;
    std::uint64_t serial_;
    float matrix_[12]{};
    bool finite_ = true;
    bool copied_ = false;
public:
    WristMatrixProvider(const WristEffectOwners& owners, std::uint64_t serial, const float (&matrix)[12])
        : owners_(&owners), serial_(serial) {
        std::memcpy(matrix_, matrix, sizeof(matrix_));
        for (float value : matrix_) finite_ &= std::isfinite(value);
    }
    WristMatrixProvider(const WristMatrixProvider&) = delete;
    WristMatrixProvider& operator=(const WristMatrixProvider&) = delete;
    WristMatrixDescriptor descriptor() { return {this, serial_}; }
    bool copied() const { return copied_; }
};
static_assert(std::is_standard_layout_v<WristMatrixProvider>);

}

namespace self_recall::pure {

inline bool relativeEffectMatrix(const float wrist[12], const float effect[12], float local[12]) {
    for (unsigned i = 0; i < 12; ++i)
        if (!std::isfinite(wrist[i]) || !std::isfinite(effect[i])) return false;
    const double a = wrist[0], b = wrist[1], c = wrist[2];
    const double d = wrist[4], e = wrist[5], f = wrist[6];
    const double g = wrist[8], h = wrist[9], j = wrist[10];
    const double det = a * (e*j-f*h) - b * (d*j-f*g) + c * (d*h-e*g);
    if (!std::isfinite(det) || std::abs(det) < 1e-10) return false;
    const double inverse[9]{(e*j-f*h)/det, (c*h-b*j)/det, (b*f-c*e)/det,
                            (f*g-d*j)/det, (a*j-c*g)/det, (c*d-a*f)/det,
                            (d*h-e*g)/det, (b*g-a*h)/det, (a*e-b*d)/det};
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned col = 0; col < 4; ++col) {
            double value = 0;
            for (unsigned k = 0; k < 3; ++k)
                value += inverse[row * 3 + k] * (double(effect[k * 4 + col]) -
                         (col == 3 ? wrist[k * 4 + 3] : 0));
            local[row * 4 + col] = float(value);
            if (!std::isfinite(local[row * 4 + col])) return false;
        }
    }
    return true;
}

inline bool emitterMatrix(const float wrist[12], const float local[12],
                          const float origin[3], float columns[16]) {
    for (unsigned col = 0; col < 4; ++col) {
        for (unsigned row = 0; row < 3; ++row) {
            double value = col == 3 ? double(wrist[row * 4 + 3]) + origin[row] : 0;
            for (unsigned k = 0; k < 3; ++k)
                value += double(wrist[row * 4 + k]) * local[k * 4 + col];
            columns[col * 4 + row] = float(value);
            if (!std::isfinite(columns[col * 4 + row])) return false;
        }
        columns[col * 4 + 3] = 0;
    }
    return true;
}

struct WristEmitterFrame {
    std::uintptr_t emitterSet = 0;
    std::uint32_t instance = 0;
    WristEffectOwner owner{};
    float local[12]{};
};

template<std::size_t Capacity = 16>
class WristEmitterFrames {
    std::atomic_flag gate_ = ATOMIC_FLAG_INIT;
    std::array<WristEmitterFrame, Capacity> frames_{};
    struct Lock {
        std::atomic_flag& gate;
        explicit Lock(std::atomic_flag& g) : gate(g) { while (gate.test_and_set(std::memory_order_acquire)) {} }
        ~Lock() { gate.clear(std::memory_order_release); }
    };
public:
    void clear() { Lock lock(gate_); frames_ = {}; }
    bool put(const WristEmitterFrame& frame) {
        if (!frame.emitterSet || !frame.owner) return false;
        Lock lock(gate_);
        WristEmitterFrame* free = nullptr;
        for (auto& entry : frames_) {
            if (entry.emitterSet == frame.emitterSet) { entry = frame; return true; }
            if (!entry.emitterSet) free = &entry;
        }
        if (!free) return false;
        *free = frame;
        return true;
    }
    WristEmitterFrame get(std::uintptr_t set, std::uint32_t instance) {
        Lock lock(gate_);
        for (const auto& entry : frames_)
            if (set && entry.emitterSet == set && entry.instance == instance) return entry;
        return {};
    }
};
}

namespace self_recall::pure {

enum class ModelQueueLane : unsigned { Single = 1, Multi = 2 };
enum class ModelJoinStatus { Waiting, Complete, UnknownScene, WrongEpoch };

template<unsigned Capacity = 64>
class ModelCompletionJoin {
    struct Slot {
        std::atomic<std::uintptr_t> queue{0};
        std::atomic<unsigned> completed{0};
    };
    std::array<Slot, Capacity> slots_{};
    std::atomic<std::uint64_t> epoch_{0};
public:
    void beginFrame(std::uint64_t epoch) {
        for (auto& slot : slots_) {
            slot.queue.store(0, std::memory_order_relaxed);
            slot.completed.store(0, std::memory_order_relaxed);
        }
        epoch_.store(epoch, std::memory_order_release);
    }
    bool registerScene(std::uintptr_t queue, std::uint64_t epoch) {
        if (!queue || !epoch || epoch != epoch_.load(std::memory_order_acquire)) return false;
        for (auto& slot : slots_) {
            auto expected = std::uintptr_t{0};
            if (slot.queue.compare_exchange_strong(expected, queue, std::memory_order_acq_rel) ||
                expected == queue) return true;
        }
        return false;
    }
    ModelJoinStatus complete(std::uintptr_t queue, std::uint64_t epoch, ModelQueueLane lane) {
        if (!epoch || epoch != epoch_.load(std::memory_order_acquire)) return ModelJoinStatus::WrongEpoch;
        for (auto& slot : slots_) {
            if (!queue || slot.queue.load(std::memory_order_acquire) != queue) continue;
            const auto bit = static_cast<unsigned>(lane);
            const auto previous = slot.completed.fetch_or(bit, std::memory_order_acq_rel);
            return previous != 3 && (previous | bit) == 3
                ? ModelJoinStatus::Complete : ModelJoinStatus::Waiting;
        }
        return ModelJoinStatus::UnknownScene;
    }
};

}

namespace self_recall::pure {
enum class ArchiveLife : unsigned { Empty, Constructing, Ready, Retiring, Destroyed };

constexpr bool archiveSuppressesUnselectedShapes(ArchiveLife life) {
    return life == ArchiveLife::Ready || life == ArchiveLife::Retiring;
}
}
