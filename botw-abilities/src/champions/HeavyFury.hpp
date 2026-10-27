// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>

namespace botw_abilities::champions {
struct HeavyFuryKey {
    std::uintptr_t action{}, player{}, scene{};
    std::uint64_t generation{};
    constexpr bool operator==(const HeavyFuryKey&) const = default;
};

class HeavyFury {
public:
    // One native stamina wheel is 1000 units, independent of purchased wheels.
    static constexpr float kRequiredStamina = 500.0f;

    bool begin(HeavyFuryKey key, float stamina) {
        Guard guard{lock_};
        clear();
        if (!key.action || !key.player || !key.scene || !valid(stamina)) return false;
        key_ = key;
        previous_ = stamina;
        return true;
    }

    bool advance(HeavyFuryKey key, float stamina, bool admitted) {
        Guard guard{lock_};
        if (!key_.action || key != key_) return false;
        if (!admitted || !valid(stamina)) { clear(); return false; }
        if (stamina < previous_) spent_ += previous_ - stamina;
        previous_ = stamina;
        if (armed_ || spent_ < kRequiredStamina) return false;
        armed_ = true;
        return true;
    }

    bool release(HeavyFuryKey key, bool attackHeld) {
        Guard guard{lock_};
        if (!key_.action || key != key_ || attackHeld || !armed_ || fired_) return false;
        fired_ = true;
        return true;
    }

    void end(std::uintptr_t action) {
        Guard guard{lock_};
        if (key_.action == action) clear();
    }
    void reset() { Guard guard{lock_}; clear(); }
    bool charging() { Guard guard{lock_}; return key_.action && armed_ && !fired_; }

private:
    struct Guard {
        std::atomic_flag& lock;
        explicit Guard(std::atomic_flag& flag) : lock(flag) {
            while (lock.test_and_set(std::memory_order_acquire)) {}
        }
        ~Guard() { lock.clear(std::memory_order_release); }
    };
    static bool valid(float stamina) { return std::isfinite(stamina) && stamina >= 0; }
    void clear() { key_ = {}; previous_ = spent_ = 0; armed_ = fired_ = false; }
    std::atomic_flag lock_{};
    HeavyFuryKey key_{};
    float previous_{}, spent_{};
    bool armed_{}, fired_{};
};
} // namespace botw_abilities::champions
