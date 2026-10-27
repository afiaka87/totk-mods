// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace totk::engine::motion {
using Vector = std::array<float, 3>;
struct Rotation { float yaw{}, pitch{}; };
struct View { Vector eye, at, auxiliary; float fovy; };
static_assert(sizeof(View) == 40);
struct Frame {
    std::uintptr_t camera{}, scene{};
    std::uint64_t clock{};
    bool aiming{}, running{}, wheelOpen{};
};
struct Sample {
    Rotation rate;
    bool permitted{}, fresh{}, invertHorizontal{}, invertVertical{};
};
struct AimState {
    std::uintptr_t camera{}, scene{};
    std::uint64_t clock{};
    Rotation angle{}, target{};
};
inline constexpr float kTau = 6.2831853f, kPitchLimit = 1.30f;

inline bool finite(Vector v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}
inline Vector subtract(Vector a, Vector b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
inline float length(Vector v) {
    return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}

// Ultrahand targeting uses world-vertical yaw and controller-local pitch, in radians/second.
[[nodiscard]] inline Rotation targetingRate(Vector turnsPerSecond, Vector worldUp) {
    const float yaw = turnsPerSecond[0]*worldUp[0] + turnsPerSecond[1]*worldUp[1] + turnsPerSecond[2]*worldUp[2];
    return {yaw * kTau * 1.65f, -turnsPerSecond[0] * kTau * 1.4f};
}

[[nodiscard]] inline AimState advance(AimState state, Frame frame, Sample sample) {
    const auto previous = state.clock;
    if (!frame.aiming || !frame.camera || !frame.scene || !sample.permitted ||
        state.camera != frame.camera || state.scene != frame.scene)
        return {frame.camera, frame.scene, frame.clock};
    state.clock = frame.clock;
    if (!frame.running || frame.wheelOpen || !sample.fresh || frame.clock < previous) return state;
    if (frame.clock == previous) return state;
    const float frames = float(double(frame.clock - previous) / 4294967296.0);
    const float seconds = frames / 30;
    const Rotation delta{sample.rate.yaw * seconds, sample.rate.pitch * seconds};
    if (frames > 3 || !std::isfinite(delta.yaw) || !std::isfinite(delta.pitch) ||
        std::fabs(delta.yaw) > kTau/2 || std::fabs(delta.pitch) > kTau/2) return state;
    state.target.yaw = std::remainder(state.target.yaw + delta.yaw * (sample.invertHorizontal ? 1 : -1), kTau);
    state.target.pitch = std::clamp(state.target.pitch + delta.pitch * (sample.invertVertical ? -1 : 1),
                                    -kPitchLimit, kPitchLimit);
    // Native bow and SpecialPower gyro follow half the remaining angle per 30 Hz frame.
    const float follow = 1 - std::pow(0.5f, frames);
    state.angle.yaw = std::remainder(state.angle.yaw + follow * std::remainder(state.target.yaw - state.angle.yaw, kTau), kTau);
    state.angle.pitch += follow * (state.target.pitch - state.angle.pitch);
    return state;
}

// Orbit the entire rig around Link without changing its radius or field of view.
[[nodiscard]] inline std::optional<View> orbit(View view, Vector pivot, Rotation angle) {
    if (!finite(view.eye) || !finite(view.at) || !finite(pivot) ||
        !std::isfinite(angle.yaw) || !std::isfinite(angle.pitch)) return {};
    const auto direction = subtract(view.at, view.eye);
    const float distance = length(direction), horizontal = std::hypot(direction[0], direction[2]);
    if (distance < 0.01f || horizontal < 0.0001f || !std::isfinite(distance)) return {};
    const float yawCos = std::cos(angle.yaw), yawSin = std::sin(angle.yaw);
    const auto yaw = [=](Vector v) -> Vector {
        return {v[0]*yawCos - v[2]*yawSin, v[1], v[0]*yawSin + v[2]*yawCos};
    };
    const auto forward = yaw(direction);
    const Vector right{-forward[2]/horizontal, 0, forward[0]/horizontal};
    const float elevation = std::atan2(direction[1], horizontal);
    const float pitch = std::clamp(elevation + angle.pitch, -kPitchLimit, kPitchLimit) - elevation;
    const float pitchCos = std::cos(pitch), pitchSin = std::sin(pitch);
    const auto rotate = [&](Vector v) -> Vector {
        v = yaw(v);
        const float dot = right[0]*v[0] + right[2]*v[2];
        return {v[0]*pitchCos - right[2]*v[1]*pitchSin + right[0]*dot*(1-pitchCos),
                v[1]*pitchCos + (right[2]*v[0] - right[0]*v[2])*pitchSin,
                v[2]*pitchCos + right[0]*v[1]*pitchSin + right[2]*dot*(1-pitchCos)};
    };
    view.eye = rotate(subtract(view.eye, pivot));
    view.at = rotate(subtract(view.at, pivot));
    for (unsigned i = 0; i < 3; ++i) {
        view.eye[i] += pivot[i];
        view.at[i] += pivot[i];
    }
    return finite(view.eye) && finite(view.at) ? std::optional{view} : std::nullopt;
}
} // namespace totk::engine::motion
