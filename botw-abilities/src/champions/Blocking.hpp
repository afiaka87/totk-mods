// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "SavedState.hpp"
#include <cstring>
#include <span>

namespace botw_abilities::champions {
struct AttackId {
    std::uint32_t serial = 0, kind = 0;
    bool operator==(const AttackId&) const = default;
};
struct AttackKey {
    AttackId primary, secondary;
    explicit operator bool() const { return primary.serial != 0; }
    bool operator==(const AttackKey&) const = default;
};
static_assert(sizeof(AttackId) == 8 && sizeof(AttackKey) == 16);
enum class BlockStatus : std::uint8_t { Blocked, Repeated, Inactive, MissingId, Stale, Unavailable };
struct BlockResult {
    BlockStatus status = BlockStatus::Inactive;
    ChargeReservation claim;
    explicit operator bool() const { return status == BlockStatus::Blocked || status == BlockStatus::Repeated; }
};

struct EquipmentWearProbe { bool blocked = false; std::uint8_t flagDelta = 0; };
template<class Evaluate, class Active, class Protected, class Clear>
EquipmentWearProbe probeEquipmentWear(std::span<const std::byte, 32> prefix, Evaluate evaluate,
                                      Active active, Protected protectedContacts, Clear clear) {
    alignas(16) std::array<std::byte, 32> copy;
    std::memcpy(copy.data(), prefix.data(), copy.size());
    evaluate(copy.data());
    if (active() && protectedContacts()) { clear(); return {true}; }
    return {false, std::uint8_t(std::to_integer<unsigned>(copy[24]) & ~std::to_integer<unsigned>(prefix[24]))};
}

// Native guard aggregates retain only the primary ID, so mixed secondary contacts cannot inherit protection.
template<std::size_t Capacity = 64>
class GuardContacts {
public:
    bool record(std::uint64_t generation, std::uint64_t scene, std::uint64_t tick,
                AttackId primary, bool protectedHit) {
        Guard guard{lock_};
        if (generation_ != generation || scene_ != scene || tick_ != tick) {
            entries_ = {}; count_ = 0; overflow_ = false;
            generation_ = generation; scene_ = scene; tick_ = tick;
        }
        if (!primary.serial) { overflow_ = true; return false; }
        for (std::size_t i = 0; i < count_; ++i)
            if (entries_[i].primary == primary) {
                entries_[i].protectedHit &= protectedHit;
                return !overflow_;
            }
        if (count_ == Capacity) { overflow_ = true; return false; }
        entries_[count_++] = {primary, protectedHit};
        return !overflow_;
    }

    bool protectedPrimary(std::uint64_t generation, std::uint64_t scene, std::uint64_t tick,
                          AttackId primary) const {
        Guard guard{lock_};
        if (overflow_ || !primary.serial || generation_ != generation || scene_ != scene || tick_ != tick) return false;
        for (std::size_t i = 0; i < count_; ++i)
            if (entries_[i].primary == primary) return entries_[i].protectedHit;
        return false;
    }

private:
    static_assert(Capacity > 0);
    struct Entry { AttackId primary; bool protectedHit = false; };
    struct Guard {
        std::atomic_flag& flag;
        explicit Guard(std::atomic_flag& value) : flag(value) { while (flag.test_and_set(std::memory_order_acquire)) {} }
        ~Guard() { flag.clear(std::memory_order_release); }
    };
    mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::array<Entry, Capacity> entries_{};
    std::uint64_t generation_ = UINT64_MAX, scene_ = 0, tick_ = 0;
    std::size_t count_ = 0;
    bool overflow_ = false;
};

template<std::size_t Capacity = 32>
class BlockLedger {
public:
    // The native player hurt-immunity window is30 frames; ticks use the shared32.32 frame clock.
    static constexpr std::uint64_t window = 30ull << 32;

    bool observeGuard(std::uint64_t generation, std::uint64_t scene, bool guarding) {
        Guard guard{lock_};
        synchronize(generation, scene);
        held_ = guarding;
        if (!guarding) spent_ = false;
        return guarding && !spent_;
    }

    BlockResult block(SavedState& saved, std::uint64_t generation, std::uint64_t scene,
                      Context context, bool guarding, AttackKey attack, std::uint64_t tick) {
        if (!context.gameplay || context.shrine || !guarding) return {};
        if (!attack) return {BlockStatus::MissingId};
        Guard guard{lock_};
        const auto snapshot = saved.snapshot();
        if (!snapshot.ready || snapshot.generation != generation) return {BlockStatus::Stale};
        synchronize(generation, scene);
        if (!snapshot.states[unsigned(Blessing::Daruk)].enabled) return {BlockStatus::Unavailable};
        for (const auto& entry : entries_)
            if (entry.used && entry.attack == attack && tick - entry.tick < window)
                return {BlockStatus::Repeated};
        if (spent_ || !held_) return {};
        const auto claim = saved.reserve(generation, Blessing::Daruk, context);
        if (!claim) return {BlockStatus::Unavailable};
        if (!saved.finish(claim, true)) return {BlockStatus::Stale};
        entries_[next_] = {attack, tick, true};
        next_ = (next_ + 1) % Capacity;
        spent_ = true;
        return {BlockStatus::Blocked, claim};
    }

private:
    static_assert(Capacity > 0);
    struct Entry { AttackKey attack; std::uint64_t tick = 0; bool used = false; };
    struct Guard {
        std::atomic_flag& flag;
        explicit Guard(std::atomic_flag& value) : flag(value) { while (flag.test_and_set(std::memory_order_acquire)) {} }
        ~Guard() { flag.clear(std::memory_order_release); }
    };
    std::array<Entry, Capacity> entries_{};
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::uint64_t generation_ = UINT64_MAX, scene_ = 0;
    std::size_t next_ = 0;
    bool spent_ = false;
    bool held_ = true;
    void synchronize(std::uint64_t generation, std::uint64_t scene) {
        if (generation_ == generation && scene_ == scene) return;
        entries_ = {}; next_ = 0; spent_ = false; held_ = true;
        generation_ = generation; scene_ = scene;
    }
};
} // namespace botw_abilities::champions
