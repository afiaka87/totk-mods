// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include "State.hpp"
#include <atomic>

namespace botw_abilities::champions {

using States = std::array<State, kBlessingCount>;

inline bool valid(const States& states) {
    for (std::size_t i = 0; i < states.size(); ++i)
        if (!valid(static_cast<Blessing>(i), states[i])) return false;
    return true;
}

struct SavedSnapshot {
    States states{};
    std::uint64_t generation = 0;
    bool ready = false;
};

struct ChargeReservation {
    std::uint64_t generation = 0, serial = 0;
    Blessing blessing{};
    Use use{};
    explicit operator bool() const { return use.consumed; }
};

// Every critical section is bounded arithmetic/copying; none calls the engine or another lock.
class SavedState {
public:
    SavedSnapshot snapshot() const {
        Guard guard{lock_};
        auto snapshot = value_;
        for (std::size_t i = 0; i < kBlessingCount; ++i)
            if (pending_[i].serial) {
                snapshot.states[i].charges = pending_[i].before.charges;
                snapshot.states[i].remainingSeconds = pending_[i].before.remainingSeconds;
            }
        return snapshot;
    }

    std::uint64_t invalidate() {
        Guard guard{lock_};
        value_.ready = false;
        pending_ = {};
        return ++value_.generation;
    }

    bool restore(std::uint64_t generation, const States& states) {
        if (!valid(states)) return false;
        Guard guard{lock_};
        if (value_.generation != generation || value_.ready) return false;
        value_.states = states;
        value_.ready = true;
        return true;
    }

    bool enable(std::uint64_t generation, Blessing blessing, bool enabled) {
        Guard guard{lock_};
        if (!value_.ready || value_.generation != generation) return false;
        value_.states[static_cast<std::size_t>(blessing)].enabled = enabled;
        return true;
    }

    Use use(Blessing blessing, const Context& context) {
        Guard guard{lock_};
        if (!value_.ready || pending_[static_cast<std::size_t>(blessing)].serial)
            return {{}, false, Refusal::Paused};
        auto& state = value_.states[static_cast<std::size_t>(blessing)];
        const auto result = consume(blessing, state, context);
        state = result.state;
        return result;
    }

    ChargeReservation reserve(std::uint64_t generation, Blessing blessing, const Context& context) {
        Guard guard{lock_};
        const auto i = static_cast<std::size_t>(blessing);
        if (!value_.ready || value_.generation != generation || pending_[i].serial)
            return {generation, 0, blessing, {{}, false, Refusal::Paused}};
        const auto before = value_.states[i];
        const auto result = consume(blessing, before, context);
        if (!result.consumed) return {generation, 0, blessing, result};
        if (++serial_ == 0) ++serial_;
        const auto serial = serial_;
        pending_[i] = {serial, before};
        value_.states[i] = result.state;
        return {generation, serial, blessing, result};
    }

    bool finish(const ChargeReservation& reservation, bool accepted) {
        if (!reservation) return false;
        Guard guard{lock_};
        const auto i = static_cast<std::size_t>(reservation.blessing);
        if (!value_.ready || value_.generation != reservation.generation ||
            pending_[i].serial != reservation.serial) return false;
        if (!accepted) {
            value_.states[i].charges = pending_[i].before.charges;
            value_.states[i].remainingSeconds = pending_[i].before.remainingSeconds;
        }
        pending_[i] = {};
        return true;
    }

    std::uint8_t tick(const std::array<Context, kBlessingCount>& contexts, float seconds) {
        Guard guard{lock_};
        if (!value_.ready) return 0;
        std::uint8_t recharged = 0;
        for (std::size_t i = 0; i < kBlessingCount; ++i) {
            if (pending_[i].serial) continue;
            const auto result = advance(static_cast<Blessing>(i), value_.states[i], contexts[i], seconds);
            value_.states[i] = result.state;
            if (result.completed) recharged |= std::uint8_t(1u << i);
        }
        return recharged;
    }


private:
    struct Guard {
        std::atomic_flag& lock;
        explicit Guard(std::atomic_flag& flag) : lock(flag) {
            while (lock.test_and_set(std::memory_order_acquire)) {}
        }
        ~Guard() { lock.clear(std::memory_order_release); }
    };
    mutable std::atomic_flag lock_{};
    SavedSnapshot value_{};
    struct Pending { std::uint64_t serial = 0; State before{}; };
    std::array<Pending, kBlessingCount> pending_{};
    std::uint64_t serial_ = 0;
};

} // namespace botw_abilities::champions
