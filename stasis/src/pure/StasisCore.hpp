// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cmath>
#include <cstdint>

namespace stasis {
struct Vec3 {
    float x{}, y{}, z{};
};
inline bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 scale(Vec3 v, float factor) { return {v.x * factor, v.y * factor, v.z * factor}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
// Keeps BotW's multiply-then-add order; the build turns off fused multiply-add.
inline float length(Vec3 v) { return std::sqrt(dot(v, v)); }

struct Ray {
    Vec3 eye{}, direction{};
    bool valid{};
};

struct Parameters {
    float reloadFrames{300.0f};
    float blowAngleDegrees{15.0f};
    float blowSpeedLimit{3.0f};
    int smallSwordCount{12};
    int largeSwordCount{6};
    int spearCount{15};
    float longFrames{300.0f};
    float middleFrames{150.0f};
    float shortFrames{60.0f};
};

// One accepted hit as BotW's charge routine sees it.
struct Strike {
    Vec3 vector;
    Vec3 point;
    int weaponKind{-1}; // -1 unclassified, 0 one-handed sword, 1 two-handed sword, else spear
    bool counted{true};
    bool raised{true};
    bool deferDamage{true};
};

struct ChargeState {
    Vec3 vector;
    Vec3 point;
    std::uint8_t counter{};
    bool charged{};
    bool firstEffectPending{};
    std::uint32_t queuedDamageCount{};
    static constexpr std::uint32_t capacity = 8;
};

// BotW's charge routine, rewritten instruction by instruction; actorScalar is the body's mass.
// Returns true when BotW would send its damage-queue notification.
inline bool acceptStrike(ChargeState &state, const Strike &strike, float actorScalar,
                         bool playerAvailable, const Parameters &parameters = {}) {
    state.point = strike.point;
    float multiplier = 1.0f;
    if (strike.counted) {
        const auto next = static_cast<std::uint32_t>(state.counter) + 1;
        state.counter =
            static_cast<std::uint8_t>(next <= ChargeState::capacity ? next : ChargeState::capacity);
        multiplier = static_cast<float>(state.counter);
    }
    float magnitude = multiplier * length(strike.vector);
    if (playerAvailable && strike.weaponKind != -1 && actorScalar > 0.0f) {
        int count = strike.weaponKind == 0   ? parameters.smallSwordCount
                    : strike.weaponKind == 1 ? parameters.largeSwordCount
                                             : parameters.spearCount;
        if (count <= 1)
            count = 1;
        const float contribution = (parameters.blowSpeedLimit * 30.0f) / static_cast<float>(count);
        magnitude = contribution * actorScalar;
    }
    if (!(magnitude > 0.0f))
        return false;
    if (state.charged)
        magnitude = magnitude + length(state.vector);
    else
        state.charged = true;

    if (playerAvailable && strike.counted && strike.raised) {
        float x = strike.vector.x;
        const float z = strike.vector.z;
        float resultZ;
        const float horizontalLength = std::sqrt((x * x + 0.0f) + z * z);
        if (horizontalLength <= 0.0f) {
            resultZ = magnitude;
        } else {
            const float factor = magnitude / horizontalLength;
            x = x * factor;
            resultZ = z * factor;
        }
        const float radians = parameters.blowAngleDegrees * 0.01745329238474369f;
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        state.vector = {x * cosine, magnitude * sine, resultZ * cosine};
    } else {
        state.vector = strike.vector;
        const float inputLength = length(strike.vector);
        if (inputLength > 0.0f)
            state.vector = scale(state.vector, magnitude / inputLength);
    }
    state.firstEffectPending = true;
    return !strike.deferDamage;
}

// BotW's charge ratio, which also sizes the arrow.
inline float chargeRatio(const ChargeState &state, float actorScalar, bool playerAvailable,
                         const Parameters &parameters = {}) {
    float ratio;
    if (state.charged) {
        ratio = actorScalar > 0.0f && playerAvailable
                    ? length(state.vector) / (actorScalar * (parameters.blowSpeedLimit * 30.0f))
                    : 1.0f;
        if (ratio > 1.0f)
            ratio = 1.0f;
    } else {
        ratio =
            static_cast<float>(state.queuedDamageCount) / static_cast<float>(ChargeState::capacity);
    }
    return ratio < 0.009999999776482582f ? 0.009999999776482582f : ratio;
}

// BotW clamps a copy at release and keeps the stored charge vector.
inline Vec3 releaseImpulse(const ChargeState &state, float actorScalar, bool playerAvailable,
                           const Parameters &parameters = {}) {
    Vec3 impulse = state.vector;
    if (state.charged && playerAvailable && actorScalar > 0.0f) {
        const float ratio =
            length(state.vector) / (actorScalar * (parameters.blowSpeedLimit * 30.0f));
        if (ratio > 1.0f)
            impulse = scale(impulse, 1.0f / ratio);
    }
    return impulse;
}
} // namespace stasis
