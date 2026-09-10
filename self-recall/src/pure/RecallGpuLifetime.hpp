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

} // namespace self_recall::pure
