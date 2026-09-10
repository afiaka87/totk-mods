#pragma once
#include <atomic>
#include "RecallPoseHistory.hpp"

namespace self_recall::pure {
template<class Sample = PoseFrameKey>
class PresentationFrameLatch {
    std::atomic_flag gate_ = ATOMIC_FLAG_INIT;
    std::uint64_t session_ = 0, clock_ = 0;
    Sample key_{};
public:
    Sample publish(std::uint64_t session, std::uint64_t clock, Sample candidate) {
        if (!session || !clock) return {};
        while (gate_.test_and_set(std::memory_order_acquire)) {}
        Sample result;
        if (session == session_ && clock == clock_) result = key_;
        else if (candidate && (session > session_ || (session == session_ && clock > clock_))) {
            session_ = session;
            clock_ = clock;
            key_ = candidate;
            result = key_;
        }
        gate_.clear(std::memory_order_release);
        return result;
    }
    Sample snapshot(std::uint64_t session) {
        while (gate_.test_and_set(std::memory_order_acquire)) {}
        const auto result = session == session_ ? key_ : Sample{};
        gate_.clear(std::memory_order_release);
        return result;
    }
};
}
