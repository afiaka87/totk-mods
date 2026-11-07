// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "CliffSearch.hpp"
#include <cmath>

namespace botw_abilities::wolf {

CliffDirections cliffSearchDirections(totk::core::WorldPosition forward) {
    constexpr float step = 0x1.921fb6p+2f / kCliffDirectionCount;
    const float sine = std::sin(step), cosine = std::cos(step);
    // Retain the original matrix operation order, including its zero terms.
    const float zeroCosine = cosine * 0.0f, zeroSine = sine * 0.0f;
    const float vertical = zeroSine * 0.0f + 1.0f;
    CliffDirections directions;
    for (auto& direction : directions) {
        direction = forward;
        forward = {
            (zeroSine * forward.y + cosine * forward.x) + (sine + 0.0f) * forward.z,
            (vertical * forward.y + zeroCosine * forward.x) + zeroSine * forward.z,
            cosine * forward.z + (zeroCosine * forward.y - sine * forward.x),
        };
    }
    return directions;
}

void CliffEscapeChoice::consider(totk::core::WorldPosition start, totk::core::WorldPosition endpoint,
                                bool reached, totk::core::WorldPosition reference) {
    if (reached) {
        const float x = endpoint.x - reference.x, z = endpoint.z - reference.z;
        const float distance = x * x + z * z;
        if (!reached_ || distance < nearest_) {
            nearest_ = distance;
            endpoint_ = endpoint;
        }
        reached_ = true;
    } else if (!reached_) {
        const float x = start.x - endpoint.x, z = start.z - endpoint.z;
        const float distance = x * x + z * z;
        if (distance > longest_) {
            longest_ = distance;
            endpoint_ = endpoint;
        }
    }
}

}
