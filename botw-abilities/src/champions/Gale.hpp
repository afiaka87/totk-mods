// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include "State.hpp"

namespace botw_abilities::champions {

enum class GalePhase : std::uint8_t { Idle, Jumping, Settling, Charging, Ready };

struct Gale {
    GalePhase phase = GalePhase::Idle;
    float elapsedSeconds = 0.0f;
    bool airborneSeen = false;
};

struct GaleInput {
    bool jumpStarted = false;
    bool jumpHeld = false;
    bool grounded = false;
    bool stopped = false;
    bool interrupted = false;
    float seconds = 0.0f;
};

struct GaleStep {
    Gale state;
    bool beginChannel = false;
    bool becameReady = false;
    bool endChannel = false;
    bool launch = false;
};

constexpr bool channeling(GalePhase phase) {
    return phase == GalePhase::Charging || phase == GalePhase::Ready;
}
constexpr bool holdsMovement(GalePhase phase) {
    return phase == GalePhase::Settling || channeling(phase);
}

inline GaleStep stepGale(Gale state, const State& charges, const Context& context,
                         const GaleInput& input, float channelSeconds) {
    if (!context.gameplay) return {state};
    const bool active = channeling(state.phase);
    if (input.interrupted || admission(charges, context) != Refusal::None ||
        !std::isfinite(channelSeconds) || channelSeconds <= 0.0f)
        return {{}, false, false, active};

    if (state.phase == GalePhase::Idle) {
        if (input.jumpStarted && input.jumpHeld) state.phase = GalePhase::Jumping;
        return {state};
    }
    if (!input.jumpHeld)
        return {{}, false, false, active,
                state.phase == GalePhase::Ready && input.grounded && input.stopped};
    if (state.phase == GalePhase::Jumping) {
        if (!input.grounded) {
            state.airborneSeen = true;
            return {state};
        }
        if (!state.airborneSeen) return {state};
        state.phase = GalePhase::Settling;
    }
    if (state.phase == GalePhase::Settling) {
        if (!input.grounded) return {};
        if (!input.stopped) return {state};
        return {{GalePhase::Charging, 0.0f}, true};
    }
    if (!input.grounded || !input.stopped) return {{}, false, false, active};
    if (state.phase == GalePhase::Ready) return {state};
    if (std::isfinite(input.seconds) && input.seconds > 0.0f)
        state.elapsedSeconds += input.seconds;
    if (state.elapsedSeconds < channelSeconds) return {state};
    return {{GalePhase::Ready, channelSeconds}, false, true};
}

} // namespace botw_abilities::champions
