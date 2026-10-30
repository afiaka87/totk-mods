// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <algorithm>
#include <cmath>

namespace material_columns {
// Same approach-to-target rule as native SlideListControl2A66168. Parameters come
// from the live stock controller, including frame-rate compensation.
inline float scrollToward(float current, float target, float frames, float fraction, float minimum) {
    if (!(frames > 0) || !std::isfinite(frames)) return current;
    const float distance = target - current;
    fraction = std::clamp(fraction, 0.f, 1.f);
    const float amount = std::max(std::abs(distance) * (1.f - std::pow(1.f - fraction, frames)),
                                  std::max(0.f, minimum) * frames);
    return std::abs(distance) <= amount ? target : current + std::copysign(amount, distance);
}
inline constexpr float rowPitch = 180.f;
// Native background masks provide directional gradients. Only moving tiles beyond
// the three-row window need whole-pane opacity; settled endpoint tiles stay opaque.
inline float rowOpacity(float y) {
    const float distance = std::abs(y);
    return std::clamp((rowPitch * 2.f - distance) / rowPitch, 0.f, 1.f);
}
// Native horizontal page pitch is170px. Halfway between pages, exchange the
// pooled edge column at zero opacity; settled immediate neighbors are50% opaque.
inline float outerColumnOpacity(float x) {
    const float distance = std::abs(x) / 170.f;
    return std::clamp(distance <= 1.f ? 1.f - .5f * distance : 1.5f - distance, 0.f, 1.f);
}
// Labels remain legible beside the focus, then leave with their pooled column.
inline float columnLabelOpacity(float x) {
    const float distance = std::abs(x) / 170.f;
    return std::clamp(distance <= 1.f ? 1.f - .15f * distance : 1.7f * (1.5f - distance), 0.f, 1.f);
}

}
