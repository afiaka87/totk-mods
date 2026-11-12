// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include <cstdint>

namespace botw_abilities::wolf {

[[nodiscard]] bool installGroundMotionApi(std::uintptr_t base);

// Catch-up pace while following: the wolf's travel is his animation's ground motion, so scaling it
// is the one lever that moves him faster than his gallop. Written by the AI worker each tick, read
// by the physics thread; values are clamped to [1, 1.5].
void setGroundSpeedScale(float scale);

}
