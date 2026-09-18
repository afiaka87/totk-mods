// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Terminal Parasail approach vector: the short physical run that lets native Parasail notice the
// wall.
#pragma once

#include "Vec3.hpp"

namespace zonai_hookshot::pure {
struct CaptureConfig {
    float speed = 4.0f;       // m/s: deliberately physical, not the fast carrier
    float homeWeight = 0.7f;  // reach the selected point
    float inwardWeight = 0.3f;// also press into the selected face
    float minDown = 0.15f;    // direction-space downward bias before normalize
    int timeoutTicks = 90;    // bounded main-thread observation window
    int leaveGraceTicks = 15; // Parasail may leave just before Climb updates
};

inline Vec3 captureVelocity(Vec3 link, Vec3 anchor, Vec3 outwardNormal,
                            const CaptureConfig& config = {}) {
    const Vec3 toAnchor = sub(anchor, link);
    const float anchorDistance = length(toAnchor);
    const float normalLength = length(outwardNormal);
    if (!finite3(toAnchor) || !finite3(outwardNormal) ||
        !std::isfinite(anchorDistance) || !std::isfinite(normalLength) ||
        !std::isfinite(config.speed) || config.speed <= 0.0f ||
        anchorDistance < 1e-3f) {
        return {};
    }
    const Vec3 home = mul(toAnchor, 1.0f / anchorDistance);
    const Vec3 inward = normalLength > 0.5f
        ? mul(outwardNormal, -1.0f / normalLength)
        : home;
    Vec3 direction = add(mul(home, config.homeWeight),
                         mul(inward, config.inwardWeight));
    // Keep the small downward component of ordinary glider contact; a high anchor must not turn
    // this into an upward carrier.
    if (direction.y > -config.minDown) direction.y = -config.minDown;
    const float directionLength = length(direction);
    if (!finite3(direction) || !std::isfinite(directionLength) ||
        directionLength < 1e-3f) {
        return {};
    }
    return mul(direction, config.speed / directionLength);
}

}  // namespace zonai_hookshot::pure
