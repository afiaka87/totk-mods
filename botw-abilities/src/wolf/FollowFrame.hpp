// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "FollowTarget.hpp"
#include <array>
#include <cstdint>

namespace botw_abilities::wolf {

struct FollowFrame {
    totk::core::WorldTransform transform{};
    std::array<float, 3> heading{};
    totk::core::WorldPosition anchor{};
};

void updateFollowFrame(FollowFrame& state, const totk::core::WorldTransform& leader, bool force);

}
