// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Straight-line position drive: start, direction and endpoint are frozen once; each update
// advances an exact distance and stops short of the anchor.
#pragma once

#include "Vec3.hpp"

namespace zonai_hookshot::pure {
struct PositionZipConfig {
    float speed = 60.0f;       // metres/second
    float updateRate = 60.0f;  // diagnostic assumption: one gameplay tick/update
    float standoff = 0.5f;     // stop this far outside the frozen anchor
    int maxTicks = 600;
};

enum class PositionZipResult {
    Continue,
    Reached,
    Timeout,
    Invalid,
};

struct PositionZipState {
    Vec3 start{};
    Vec3 direction{};
    Vec3 endpoint{};
    float travelDistance = 0.0f;
    float advanced = 0.0f;
    int ticks = 0;
    bool active = false;
};

inline bool beginPositionZip(PositionZipState& state, Vec3 start, Vec3 anchor,
                             const PositionZipConfig& config = {}) {
    state = {};
    const Vec3 delta = sub(anchor, start);
    const float span = length(delta);
    if (!finite3(start) || !finite3(anchor) || !std::isfinite(span) ||
        !std::isfinite(config.speed) || !std::isfinite(config.updateRate) ||
        !std::isfinite(config.standoff) || config.speed <= 0.0f ||
        config.updateRate <= 0.0f || config.standoff < 0.0f ||
        span <= config.standoff || config.maxTicks <= 0) {
        return false;
    }
    state.start = start;
    state.direction = mul(delta, 1.0f / span);
    state.travelDistance = span - config.standoff;
    state.endpoint = add(start, mul(state.direction, state.travelDistance));
    state.active = finite3(state.direction) && finite3(state.endpoint);
    return state.active;
}

inline PositionZipResult stepPositionZip(PositionZipState& state, Vec3& next,
                                         const PositionZipConfig& config = {}) {
    if (!state.active || !finite3(state.start) || !finite3(state.direction) ||
        !std::isfinite(state.travelDistance) || !std::isfinite(state.advanced) ||
        !std::isfinite(config.speed) || !std::isfinite(config.updateRate) ||
        config.speed <= 0.0f || config.updateRate <= 0.0f) {
        return PositionZipResult::Invalid;
    }
    if (++state.ticks > config.maxTicks) {
        state.active = false;
        return PositionZipResult::Timeout;
    }
    const float stepDistance = config.speed / config.updateRate;
    float nextAdvance = state.advanced + stepDistance;
    if (nextAdvance >= state.travelDistance) nextAdvance = state.travelDistance;
    state.advanced = nextAdvance;
    next = add(state.start, mul(state.direction, state.advanced));
    if (!finite3(next)) {
        state.active = false;
        return PositionZipResult::Invalid;
    }
    if (state.advanced >= state.travelDistance) {
        state.active = false;
        return PositionZipResult::Reached;
    }
    return PositionZipResult::Continue;
}

inline float positionZipRemaining(const PositionZipState& state) {
    const float remaining = state.travelDistance - state.advanced;
    return remaining > 0.0f ? remaining : 0.0f;
}

}  // namespace zonai_hookshot::pure
