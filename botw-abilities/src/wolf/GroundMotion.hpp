// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/core/Units.hpp"
#include <optional>

namespace botw_abilities::wolf {

struct GroundMotion {
    totk::core::WorldPosition facing, up, surface;
    float speed{}, forward{}, side{};
};

// The ported one-shot clips carry lunges of up to 86 units a second (Attack_Attack_WolfLink moves its
// root 20 units in 7 frames); the loops never pass the gear-4 table's 13.5. Anything above this is a
// clip authored for a different mover and is held to a hop (wolf-compact-08 readout, 2026-11-02).
inline constexpr float kMaxGroundSpeed = 15;

// Recover BotW's independent tangent-axis speeds from TotK's animation-motion encoding, with the
// speed held to kMaxGroundSpeed.
[[nodiscard]] std::optional<totk::core::WorldPosition> animationGroundVelocity(const GroundMotion& motion);

}
