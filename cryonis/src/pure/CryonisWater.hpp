// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <algorithm>
#include <cmath>
namespace cryonis::water {
inline constexpr unsigned kSide = 64;
inline constexpr float kNoWater = 20000.0f;
inline constexpr float kHighlightReach = 375.0f;
inline constexpr float kHighlightFadeStart = 300.0f;
// Match water.vert: 2 m near samples and bounded coverage at the same query budget.
inline float coordinate(unsigned index, float reach) {
    const float t = float(index) - 31.5f;
    const float tail = std::max(std::fabs(t) - 15.5f, 0.0f) / 16.0f;
    const float extra = (reach - 63.0f) * tail * tail * tail * tail;
    return 2.0f * t + std::copysign(extra, t);
}
}
