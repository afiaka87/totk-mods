// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cmath>
#include <cstdint>

namespace botw_abilities::champions {
struct EffectPulse {
    float delay = 0, remaining = 0;
    std::uint32_t serial = 0;
    bool held = false;
    bool active() const { return held || (delay <= 0 && remaining > 0); }
    bool start(float seconds, float after = 0) {
        if (!std::isfinite(seconds) || !std::isfinite(after) || seconds <= 0 || after < 0) return false;
        delay = after;
        remaining = seconds;
        held = false;
        if (++serial == 0) ++serial;
        return true;
    }
    void hold(bool active) {
        if (held == active) return;
        held = active;
        delay = remaining = 0;
        if (active && ++serial == 0) ++serial;
    }
    void tick(float seconds) {
        if (!std::isfinite(seconds) || seconds <= 0) return;
        if (delay > 0) {
            const auto consumed = seconds < delay ? seconds : delay;
            delay -= consumed;
            seconds -= consumed;
        }
        if (seconds > 0) remaining = remaining > seconds ? remaining - seconds : 0;
    }
};

enum class CarrierAction { None, Spawn, Follow, Delete };
struct CarrierState {
    EffectPulse pulse;
    std::uint32_t spawnedSerial = 0;
    float waitingSeconds = 0;
    bool pending = false, observed = false, deleting = false;
    void tick(float seconds) {
        pulse.tick(seconds);
        if (pending && std::isfinite(seconds) && seconds > 0) waitingSeconds += seconds;
    }
    void created() { pending = true; waitingSeconds = 0; spawnedSerial = pulse.serial; }
    // Loading a native field must not consume its visible lifetime.
    void tickWhileVisible(float seconds) {
        if (!std::isfinite(seconds) || seconds <= 0) return;
        if (pending) waitingSeconds += seconds;
        if (observed && !deleting && spawnedSerial == pulse.serial) pulse.tick(seconds);
    }
};
struct CarrierStep { CarrierAction action{}; bool timedOut = false; };

inline CarrierStep observeCarrier(CarrierState& state, bool alive) {
    if (alive) {
        state.pending = false;
        state.observed = true;
        if (!state.pulse.active() || state.spawnedSerial != state.pulse.serial) {
            state.deleting = true;
            return {CarrierAction::Delete};
        }
        return {CarrierAction::Follow};
    }
    if (state.deleting || state.observed) state.pending = state.observed = state.deleting = false;
    const bool timedOut = state.pending && state.waitingSeconds > 5;
    if (timedOut) state.pending = false;
    return {state.pulse.active() && !state.pending ? CarrierAction::Spawn : CarrierAction::None, timedOut};
}
} // namespace botw_abilities::champions
