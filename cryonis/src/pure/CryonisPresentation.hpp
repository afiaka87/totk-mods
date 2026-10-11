// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "CryonisCore.hpp"
#include "CryonisWater.hpp"
namespace cryonis {
inline constexpr unsigned kWaterSide = water::kSide;
inline constexpr float kNoWater = water::kNoWater;
struct Presentation {
    Pillar pillars[kPillarLimit]{};
    Target target{};
    float water[kWaterSide * kWaterSide]{};
    Vec3 waterOrigin{};
    float cooldown{}, previewFrames{}, elapsedFrames{}, breakAge{-1};
    Vec3 broken{};
    Vec3 player{};
    bool aiming{}, running{}, pending{}, canPlace{}, sceneReady{};
};
}
