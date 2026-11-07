// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/core/Units.hpp"

namespace botw_abilities::wolf {

struct TurnFacing { float remaining{}, animationAngle{}; };

[[nodiscard]] TurnFacing turnFacing(totk::core::WorldPosition position,
    totk::core::WorldPosition forward, totk::core::WorldPosition target, float maximumAngle);

}
