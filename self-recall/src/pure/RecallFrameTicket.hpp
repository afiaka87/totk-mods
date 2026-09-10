#pragma once

#include <atomic>
#include <cstring>
#include <type_traits>

#include "RecallHistory.hpp"

namespace self_recall::pure {

template <class T>
class FrameMailbox {
    static_assert(std::is_trivially_copyable_v<T>);
public:
    bool publish(const T& value) {
        if (busy_.test_and_set(std::memory_order_acquire)) return false;
        value_ = value;
        busy_.clear(std::memory_order_release);
        return true;
    }
    bool snapshot(T& out) {
        if (busy_.test_and_set(std::memory_order_acquire)) return false;
        out = value_;
        busy_.clear(std::memory_order_release);
        return true;
    }
private:
    std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
    T value_{};
};

struct ActorFrameTicket {
    std::uint64_t actor = 0;
    std::uint64_t model = 0;
    std::uint32_t actorId = 0;
    std::uint32_t worldGeneration = 0;
    HistorySample route{};
    float modelRoot[12]{};
    float waterHeight = 0;
    bool haveWaterHeight = false;
};

inline bool matchesActorFrame(const ActorFrameTicket& ticket,
                              std::uint64_t actor, std::uint32_t actorId,
                              std::uint64_t model, std::uint32_t worldGeneration,
                              const Pose& pose, const float modelRoot[12]) {
    if (!modelRoot) return false;
    for (unsigned i = 0; i < 12; ++i)
        if (!std::isfinite(modelRoot[i])) return false;
    return actor && model && worldGeneration && modelRoot &&
           ticket.actor == actor && ticket.actorId == actorId &&
           ticket.model == model && ticket.worldGeneration == worldGeneration &&
           finitePose(pose) &&
           std::memcmp(&ticket.route.pose, &pose, sizeof(pose)) == 0 &&
           std::memcmp(ticket.modelRoot, modelRoot, sizeof(ticket.modelRoot)) == 0;
}

}  // namespace self_recall::pure
