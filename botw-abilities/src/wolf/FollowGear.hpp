// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/core/Units.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>

namespace botw_abilities::wolf {

enum class FollowRange : std::uint8_t { Near, Far };
[[nodiscard]] constexpr int maximumFollowGear(FollowRange range) {
    return range == FollowRange::Near ? 2 : 4;
}

struct FollowGear {
    float remaining = 30.0f, previous = 30.0f;
    int gear = 1;
    bool restricted = true;
};
static_assert(sizeof(FollowGear) == 16);

struct FollowGearChange {
    FollowGear state;
    std::optional<bool> limitSpeed;
    bool changed{};
};

struct FollowGearTransition {
    int current{}, pending{};
    float remaining{};
    bool sameType{};
};

[[nodiscard]] constexpr int pathFollowGear(int selected, float linear, float angular,
    const totk::core::WorldPosition& position, const totk::core::WorldPosition& goal) {
    const float turnRadius = linear / angular;
    const float radius = (turnRadius + turnRadius) - 8.0f;
    if (!(radius > 0)) return selected;
    const float x = goal.x - position.x, y = goal.y - position.y, z = goal.z - position.z;
    return ((x * x + y * y) + z * z) < radius * radius ? 1 : selected;
}

[[nodiscard]] constexpr int closeFollowGear(float distance, const FollowGearTransition& transition) {
    constexpr float thresholds[]{0.0f, 2.5f, 5.5f, 10.0f};
    const int current = transition.pending ? transition.pending : transition.current;
    int desired = 4;
    for (int gear = 0; gear < 4; ++gear) {
        const float factor = current > gear ? 0.6f : 1.0f;
        if (distance < thresholds[gear] * factor) {
            desired = gear;
            break;
        }
    }
    if ((transition.current != transition.pending || !transition.sameType) && transition.remaining > 0)
        return current;
    if (!desired) return 0;
    if (desired < current) return current - 1;
    return desired == 4 && current < 3 ? 3 : desired;
}

// Probe checks the current travel direction over that gear's original scaled speed.
template<class Probe>
[[nodiscard]] FollowGear beginFollowGear(FollowRange range, Probe&& probe) {
    int gear = maximumFollowGear(range);
    while (gear > 1 && !probe(gear)) --gear;
    return {30.0f, 30.0f, gear, true};
}

template<class Probe>
[[nodiscard]] FollowGearChange advanceFollowGear(FollowGear state, FollowRange range,
                                                 float frameDelta, Probe&& probe) {
    if (state.restricted) {
        state.previous = state.remaining;
        state.remaining = -1.0f * frameDelta + state.remaining;
        if (state.remaining <= 0x1p-23f) state.restricted = false;
    }
    const int maximum = maximumFollowGear(range);
    int next = state.gear;
    std::optional<bool> limitSpeed;
    if (!probe(state.gear)) {
        next = std::clamp(state.gear - 1, 1, maximum);
        if (next != state.gear) limitSpeed = true;
    } else if (!state.restricted && probe(state.gear + 1)) {
        next = std::clamp(state.gear + 1, 1, maximum);
        if (next != state.gear && next == maximum) limitSpeed = false;
    }
    const bool changed = next != state.gear;
    if (changed) state = {30.0f, 30.0f, next, true};
    return {state, limitSpeed, changed};
}

}
