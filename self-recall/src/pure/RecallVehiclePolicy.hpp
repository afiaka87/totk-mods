#pragma once
#include <atomic>
#include "RecallHistory.hpp"

namespace self_recall::pure {
class NativeVehicleState {
public:
    void enter(std::uint32_t actorId) {
        if (actorId) actor_.store(actorId, std::memory_order_release);
    }
    void leave(std::uint32_t actorId) {
        if (actorId) actor_.compare_exchange_strong(actorId, 0, std::memory_order_acq_rel);
    }
    bool active(std::uint32_t actorId) const {
        return actorId && actor_.load(std::memory_order_acquire) == actorId;
    }
    void clear() { actor_.store(0, std::memory_order_release); }
private:
    std::atomic<std::uint32_t> actor_{0};
};

inline std::uint8_t pairVehicleAdmission(std::uint8_t flags, bool mayAdmit, bool nativeActive) {
    if (mayAdmit && (nativeActive || (flags & SampleControlStick)))
        flags |= SampleControlStick | SampleAdmissible;
    return flags;
}
}
