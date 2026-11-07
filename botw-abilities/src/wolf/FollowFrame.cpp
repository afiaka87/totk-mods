// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "FollowFrame.hpp"

namespace botw_abilities::wolf {

// Turning updates the heading first; the next straight step can capture the new frame.
void updateFollowFrame(FollowFrame& state, const totk::core::WorldTransform& leader, bool force) {
    const auto& r = leader.rotation.values;
    const std::array heading{r[2], r[5], r[8]};
    if (force) {
        state.transform = leader;
        state.heading = heading;
        state.anchor = leader.position;
        return;
    }
    const auto& previous = state.heading;
    const float dot = (previous[0] * heading[0] + previous[1] * heading[1]) + previous[2] * heading[2];
    const float x = previous[1] * heading[2] - previous[2] * heading[1];
    const float y = previous[2] * heading[0] - previous[0] * heading[2];
    const float z = previous[0] * heading[1] - previous[1] * heading[0];
    const float angle = std::atan2(std::sqrt(z * z + (x * x + y * y)), dot);
    const float dx = state.anchor.x - leader.position.x;
    const float dz = state.anchor.z - leader.position.z;
    if (captureFollowFrame(dx * dx + dz * dz, angle)) {
        state.transform = leader;
        state.heading = heading;
        state.anchor = leader.position;
        return;
    }
    if (angle >= kFollowHeadingLimit) state.heading = heading;
    state.anchor = state.transform.position;
}

}
