// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "StasisCore.hpp"
#include <algorithm>

namespace stasis::camera {
struct View {
    Vec3 eye, at, auxiliary;
    float fovy;
};
static_assert(sizeof(View) == 40);
// Eases the camera over Link's shoulder while aiming and back afterwards.
struct Blend {
    std::uintptr_t owner{}, scene{};
    float amount{};
    void reset() { *this = {}; }
    bool apply(View &view, std::uintptr_t identity, std::uintptr_t token, bool aiming,
               float frames) {
        if (!identity || !token) {
            reset();
            return false;
        }
        if (owner != identity || scene != token) {
            reset();
            owner = identity;
            scene = token;
        }
        const Vec3 delta = sub(view.at, view.eye);
        const float distance = length(delta);
        const auto side = cross(delta, {0, 1, 0});
        const float sideLength = length(side);
        if (!finite(view.eye) || !finite(view.at) || !std::isfinite(distance) ||
            !std::isfinite(sideLength) || distance < 0.01f || sideLength < 0.001f ||
            !std::isfinite(view.fovy) || view.fovy <= 0 || view.fovy >= 3.14f) {
            reset();
            return false;
        }
        if (std::isfinite(frames) && frames > 0) {
            const float step = std::min(frames, 3.0f) / 6.0f;
            amount = aiming ? std::min(amount + step, 1.0f) : std::max(amount - step, 0.0f);
        }
        if (amount == 0)
            return true;
        const auto right = scale(side, 1.0f / sideLength);
        const auto up = scale(cross(right, delta), 1.0f / distance);
        const float eased = amount * amount * (3.0f - 2.0f * amount);
        // Framing matched to BotW screenshots, not recovered BotW values.
        const float radius = std::min(distance, 8.0f);
        const auto shift = scale(add(scale(right, .12f), scale(up, .13f)), radius * eased);
        view.eye = add(view.eye, shift);
        view.at = add(view.at, shift);
        return true;
    }
};
} // namespace stasis::camera
