// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/core/Units.hpp"
#include <cmath>

namespace botw_abilities::champions {
inline constexpr float kFuryTargetRadius = 50.0f;
inline bool furyTargetInRange(totk::core::WorldPosition origin, totk::core::WorldPosition target,
                             bool hostile, float radius = kFuryTargetRadius) {
    if (!hostile || !std::isfinite(radius) || radius <= 0) return false;
    const auto x = target.x-origin.x, y = target.y-origin.y, z = target.z-origin.z;
    const auto distance = x*x+y*y+z*z;
    return std::isfinite(distance) && distance <= radius*radius;
}

} // namespace botw_abilities::champions
