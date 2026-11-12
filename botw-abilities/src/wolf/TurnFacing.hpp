// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "totk/core/Units.hpp"

namespace botw_abilities::wolf {

struct TurnFacing { float remaining{}, animationAngle{}; };

[[nodiscard]] TurnFacing turnFacing(totk::core::WorldPosition position,
    totk::core::WorldPosition forward, totk::core::WorldPosition target, float maximumAngle);

}
