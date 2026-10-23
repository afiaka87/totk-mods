// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace botw_abilities::champions {

enum class Blessing : std::uint8_t { Mipha, Revali, Daruk, Urbosa };
inline constexpr std::size_t kBlessingCount = 4;

struct Rule {
    std::uint8_t charges;
    float rechargeSeconds;
};

inline constexpr std::array<Rule, kBlessingCount> kRules{{
    {1, 1440.0f}, {3, 360.0f}, {6, 1080.0f}, {3, 720.0f},
}};

constexpr Rule rule(Blessing blessing) {
    return kRules[static_cast<std::size_t>(blessing)];
}

struct State {
    std::uint8_t charges = 0;
    float remainingSeconds = 0.0f;
    bool enabled = true;
};

constexpr State initial(Blessing blessing) {
    return {rule(blessing).charges, 0.0f, true};
}

struct Context {
    bool gameplay = false;
    bool shrine = false;
    bool ballad = false;
    bool castle = false;
};

constexpr float rechargeRate(const Context& context) {
    return (context.ballad ? 3.0f : 1.0f) * (context.castle ? 3.0f : 1.0f);
}

enum class Refusal : std::uint8_t { None, Paused, Shrine, Disabled, Recharging };

constexpr Refusal admission(const State& state, const Context& context) {
    if (!context.gameplay) return Refusal::Paused;
    if (context.shrine) return Refusal::Shrine;
    if (!state.enabled) return Refusal::Disabled;
    return state.charges ? Refusal::None : Refusal::Recharging;
}

struct Recharge {
    State state;
    bool completed = false;
};

inline Recharge advance(Blessing blessing, State state, const Context& context, float seconds) {
    if (!context.gameplay || state.charges || !std::isfinite(seconds) || seconds <= 0.0f)
        return {state};
    state.remainingSeconds -= seconds * rechargeRate(context);
    if (state.remainingSeconds > 0.0f) return {state};
    state.charges = rule(blessing).charges;
    state.remainingSeconds = 0.0f;
    return {state, true};
}

struct Use {
    State state;
    bool consumed = false;
    Refusal refusal = Refusal::None;
};

constexpr Use consume(Blessing blessing, State state, const Context& context) {
    const auto refusal = admission(state, context);
    if (refusal != Refusal::None) return {state, false, refusal};
    --state.charges;
    state.remainingSeconds = state.charges ? 0.0f : rule(blessing).rechargeSeconds;
    return {state, true};
}

inline bool valid(Blessing blessing, const State& state) {
    return state.charges <= rule(blessing).charges && std::isfinite(state.remainingSeconds) &&
           state.remainingSeconds >= 0.0f && state.remainingSeconds <= rule(blessing).rechargeSeconds &&
           (state.charges ? state.remainingSeconds == 0.0f : state.remainingSeconds > 0.0f);
}

enum class Revive : std::uint8_t { Native, Mipha };

constexpr Revive revival(const State& state, const Context& context, bool fatal, bool oneShotProtected) {
    return fatal && !oneShotProtected && admission(state, context) == Refusal::None
               ? Revive::Mipha : Revive::Native;
}

constexpr bool barrier(const State& state, const Context& context, bool guarding) {
    return guarding && admission(state, context) == Refusal::None;
}

constexpr bool fury(const State& state, const Context& context, bool chargedSpinReleased) {
    return chargedSpinReleased && admission(state, context) == Refusal::None;
}

} // namespace botw_abilities::champions
