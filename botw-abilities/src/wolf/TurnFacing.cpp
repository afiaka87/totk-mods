// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "TurnFacing.hpp"
#include "WorldMath.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace botw_abilities::wolf {

TurnFacing turnFacing(totk::core::WorldPosition position, totk::core::WorldPosition forward,
                      totk::core::WorldPosition target, float maximumAngle) {
    auto direction = horizontalUnit({target.x - position.x, 0, target.z - position.z});
    if (std::isnan(direction.x) || std::isnan(direction.y) || std::isnan(direction.z) ||
        (direction.x == 0 && direction.y == 0 && direction.z == 0)) direction = forward;
    const float dot = direction.x * forward.x + direction.y * forward.y + direction.z * forward.z;
    const float angle = std::acos(std::clamp(dot, -1.0f, 1.0f));
    const float sign = direction.z * forward.x - direction.x * forward.z < 0 ? -1.0f : 1.0f;
    constexpr float kAnimationDegrees = std::bit_cast<float>(std::uint32_t{0xC2652EE0});
    return {angle, (sign * std::clamp(angle, 0.0f, maximumAngle)) * kAnimationDegrees};
}

}
