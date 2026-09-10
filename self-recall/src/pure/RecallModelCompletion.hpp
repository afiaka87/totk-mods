#pragma once

#include <array>
#include <atomic>
#include <cstdint>

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

} // namespace self_recall::pure
