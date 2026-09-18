// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Helix reference curve and latch feedback; host tests check the shader's closed form against
// helixPoint.
#pragma once

#include <cmath>
#include <cstdint>

#include "Vec3.hpp"

namespace zonai_hookshot::pure {
// World length of one coil turn; the shader derives its turn count from the same constant.
constexpr float kHelixWavelength = 3.4f;
constexpr std::uint32_t kLatchFeedbackTicks = 30;

inline float clamp(float value, float lo, float hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

inline bool normalize(Vec3 value, Vec3& out) {
    const float magnitude = length(value);
    if (!finite3(value) || !std::isfinite(magnitude) || magnitude < 0.0001f)
        return false;
    out = mul(value, 1.0f / magnitude);
    return true;
}

struct HelixFrame {
    Vec3 axis{};
    Vec3 radialA{};
    Vec3 radialB{};
    float span = 0.0f;
    bool valid = false;
};

// World-up reference keeps the helix stable; near-vertical chains fall back to X then Z.
inline HelixFrame makeHelixFrame(Vec3 from, Vec3 to) {
    HelixFrame frame{};
    const Vec3 delta = sub(to, from);
    frame.span = length(delta);
    if (!std::isfinite(frame.span) || frame.span < 0.05f ||
        !normalize(delta, frame.axis))
        return frame;

    Vec3 radial{};
    if (!normalize(cross(frame.axis, {0.0f, 1.0f, 0.0f}), radial) &&
        !normalize(cross(frame.axis, {1.0f, 0.0f, 0.0f}), radial) &&
        !normalize(cross(frame.axis, {0.0f, 0.0f, 1.0f}), radial))
        return frame;
    frame.radialA = radial;
    if (!normalize(cross(frame.axis, frame.radialA), frame.radialB))
        return frame;
    frame.valid = true;
    return frame;
}

inline Vec3 helixPoint(Vec3 from, Vec3 to, const HelixFrame& frame, float t,
                       float radius, float phaseRadians, bool opposite) {
    constexpr float kTau = 6.2831853071795864769f;
    if (!frame.valid) return lerp(from, to, clamp01(t));
    t = clamp01(t);
    const float turns = frame.span / kHelixWavelength;
    const float theta = kTau * turns * t + phaseRadians +
                        (opposite ? 3.14159265358979323846f : 0.0f);
    const Vec3 radial = add(mul(frame.radialA, std::cos(theta)),
                            mul(frame.radialB, std::sin(theta)));
    return add(lerp(from, to, t), mul(radial, radius));
}

// Latch beat: maximum on the first latched tick, decaying over about half a second.
inline float latchFeedbackEnvelope(std::uint32_t ticksRemaining) {
    if (ticksRemaining == 0) return 0.0f;
    const float x = clamp01(static_cast<float>(ticksRemaining) /
                            static_cast<float>(kLatchFeedbackTicks));
    return x * x * (3.0f - 2.0f * x);  // smoothstep
}

inline float reticleWorldSize(float cameraDistance) {
    if (!std::isfinite(cameraDistance)) return 0.28f;
    return clamp(0.18f + cameraDistance * 0.006f, 0.24f, 0.82f);
}

}  // namespace zonai_hookshot::pure
