// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Yaw planning: Link's captured 3x3 basis is kept and only a world-up yaw is pre-multiplied.
#pragma once

#include <cmath>
#include <cstdint>

#include "Vec3.hpp"

namespace zonai_hookshot::pure {
enum class YawTiming : uint8_t {
    Early,
    OnParasail,
};

inline const char* yawTimingName(YawTiming timing) {
    return timing == YawTiming::Early ? "EARLY" : "GLIDER";
}

inline YawTiming toggledYawTiming(YawTiming timing) {
    return timing == YawTiming::Early ? YawTiming::OnParasail : YawTiming::Early;
}

struct YawConfig {
    float earlyDistance = 4.0f;
    float journeyFraction = 0.75f;
    float journeyCap = 30.0f;
    float minimumDistance = 1.0f;
};

struct YawState {
    YawTiming timing = YawTiming::Early;
    bool valid = false;
    bool started = false;
    bool finished = false;
    float signedRadians = 0.0f;
    float durationDistance = 0.0f;
    float elapsedDistance = 0.0f;
};

// Ray-hit normals point out of the face, so Link faces into the wall along -normal; start-to-
// anchor is only the degenerate fallback.
inline Vec3 wallFacingDirection(Vec3 committedNormal, Vec3 pathDirection) {
    Vec3 desired{-committedNormal.x, 0.0f, -committedNormal.z};
    if (!finite3(desired) || length(desired) < 0.05f)
        desired = {pathDirection.x, 0.0f, pathDirection.z};
    return desired;
}

inline bool finiteBasis(const float basis[9]) {
    if (!basis) return false;
    for (int i = 0; i < 9; ++i)
        if (!std::isfinite(basis[i])) return false;
    return true;
}

inline bool signedHorizontalYaw(Vec3 current, Vec3 desired, float& outRadians) {
    current.y = 0.0f;
    desired.y = 0.0f;
    if (!finite3(current) || !finite3(desired)) return false;
    const float currentLength = length(current);
    const float desiredLength = length(desired);
    if (currentLength < 0.05f || desiredLength < 0.05f) return false;
    current = mul(current, 1.0f / currentLength);
    desired = mul(desired, 1.0f / desiredLength);
    float cosine = dot(current, desired);
    if (cosine < -1.0f) cosine = -1.0f;
    if (cosine > 1.0f) cosine = 1.0f;
    const float sine = current.z * desired.x - current.x * desired.z;
    outRadians = std::atan2(sine, cosine);
    return std::isfinite(outRadians);
}

inline float yawLinearProgress(const YawState& state) {
    if (!state.valid) return 0.0f;
    if (state.finished) return 1.0f;
    if (!state.started || state.durationDistance <= 0.0f) return 0.0f;
    return clamp01(state.elapsedDistance / state.durationDistance);
}

inline float yawEasedProgress(const YawState& state) {
    const float t = yawLinearProgress(state);
    return t * t * (3.0f - 2.0f * t);  // cubic smoothstep
}

inline bool prepareYaw(YawState& state, const float basis[9], Vec3 desired,
                       float travelDistance, YawTiming timing,
                       const YawConfig& config = {}) {
    state = {};
    state.timing = timing;
    if (!finiteBasis(basis) || !finite3(desired) || !std::isfinite(travelDistance) ||
        travelDistance <= 0.0f || !std::isfinite(config.earlyDistance) ||
        !std::isfinite(config.journeyFraction) || !std::isfinite(config.journeyCap) ||
        !std::isfinite(config.minimumDistance) || config.earlyDistance <= 0.0f ||
        config.journeyFraction <= 0.0f || config.journeyFraction > 1.0f ||
        config.journeyCap <= 0.0f || config.minimumDistance <= 0.0f)
        return false;

    const Vec3 current{basis[2], 0.0f, basis[8]};
    // cross(current, desired).y is positive for +Z -> +X, matching rotateBasisWorldYaw().
    if (!signedHorizontalYaw(current, desired, state.signedRadians)) return false;

    state.valid = true;
    if (std::fabs(state.signedRadians) < 0.0001f) {
        state.started = true;
        state.finished = true;
        return true;
    }
    if (timing == YawTiming::Early) {
        state.started = true;
        state.durationDistance =
            travelDistance < config.earlyDistance ? travelDistance : config.earlyDistance;
    }
    return true;
}

// Arm the ease at the first Parasail observation; the planned endpoint is min(75% of travel, 30
// m), compressed to one metre if Parasail arrives late.
inline bool startYawOnParasail(YawState& state, float advanced,
                               float travelDistance,
                               const YawConfig& config = {}) {
    if (!state.valid || state.timing != YawTiming::OnParasail || state.started ||
        !std::isfinite(advanced) || !std::isfinite(travelDistance))
        return false;
    float finishAt = travelDistance * config.journeyFraction;
    if (finishAt > config.journeyCap) finishAt = config.journeyCap;
    float duration = finishAt - (advanced < 0.0f ? 0.0f : advanced);
    if (duration < config.minimumDistance) duration = config.minimumDistance;
    state.durationDistance = duration;
    state.elapsedDistance = 0.0f;
    state.started = true;
    return true;
}

inline void advanceYaw(YawState& state, float equivalentDistance) {
    if (!state.valid || !state.started || state.finished ||
        !std::isfinite(equivalentDistance) || equivalentDistance <= 0.0f)
        return;
    state.elapsedDistance += equivalentDistance;
    if (state.elapsedDistance >= state.durationDistance) {
        state.elapsedDistance = state.durationDistance;
        state.finished = true;
    }
}

// Pre-multiply the row-major basis by a world-Y rotation: changes yaw without touching pitch or
// roll.
inline bool rotateBasisWorldYaw(const float basis[9], float radians,
                                float out[9]) {
    if (!finiteBasis(basis) || !out || !std::isfinite(radians)) return false;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    if (!std::isfinite(c) || !std::isfinite(s)) return false;
    for (int column = 0; column < 3; ++column) {
        const float x = basis[column];
        const float y = basis[3 + column];
        const float z = basis[6 + column];
        out[column] = c * x + s * z;
        out[3 + column] = y;
        out[6 + column] = -s * x + c * z;
    }
    return finiteBasis(out);
}

inline bool easedYawBasis(const float basis[9], const YawState& state,
                          float out[9]) {
    if (!state.valid) return false;
    return rotateBasisWorldYaw(basis,
                               state.signedRadians * yawEasedProgress(state), out);
}

}  // namespace zonai_hookshot::pure
