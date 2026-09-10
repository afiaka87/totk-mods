#pragma once
#include <array>
#include <atomic>
#include <cstring>
#include "RecallHistory.hpp"

namespace self_recall::pure {
struct AppliedClimb {
    std::uintptr_t player = 0;
    std::uint32_t actorId = 0, world = 0;
    HistorySample sample{};
};
class AppliedClimbMailbox {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    static constexpr auto kWords = (sizeof(AppliedClimb) + 7) / 8;
    std::atomic<std::uint64_t> serial_{0};
    std::array<std::atomic<std::uint64_t>, kWords> words_{};
public:
    void publish(const AppliedClimb& value) {
        std::array<std::uint64_t, kWords> words{};
        std::memcpy(words.data(), &value, sizeof(value));
        serial_.fetch_add(1);
        for (unsigned i = 0; i < kWords; ++i) words_[i].store(words[i]);
        serial_.fetch_add(1);
    }
    bool snapshot(AppliedClimb& out) const {
        const auto serial = serial_.load();
        if (!serial || (serial & 1)) return false;
        std::array<std::uint64_t, kWords> words{};
        for (unsigned i = 0; i < kWords; ++i) words[i] = words_[i].load();
        if (serial_.load() != serial) return false;
        std::memcpy(&out, words.data(), sizeof(out));
        return true;
    }
};
inline bool matchesAppliedPose(const AppliedClimb& applied, std::uintptr_t player,
                               std::uint32_t actorId, std::uint32_t world) {
    return player && world && applied.player == player && applied.actorId == actorId &&
           applied.world == world &&
           (applied.sample.flags & SampleAdmissible) && finitePose(applied.sample.pose);
}
inline bool matchesAppliedClimb(const AppliedClimb& applied, std::uintptr_t player,
                               std::uint32_t actorId, std::uint32_t world) {
    return matchesAppliedPose(applied, player, actorId, world) && (applied.sample.flags & SampleClimb);
}
} // namespace self_recall::pure
