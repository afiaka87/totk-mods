// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <algorithm>
#include <cmath>
#include "BombReference.hpp"

namespace remote_bomb {
struct BlastClock {
    float remainingFrames = kBlastLifetimeFrames;
    float radius = kInitialBlastRadius;
    bool enabled = true;
};

// BombExplode::calc (BotW 1.5.0, 0xC9424) tests expiry before advancing.
inline BlastClock advanceBlast(BlastClock previous, float gameplayFrames) {
    if (!previous.enabled || !std::isfinite(gameplayFrames) || gameplayFrames <= 0.f)
        return previous;
    if (previous.remainingFrames <= 0.f) {
        previous.enabled = false;
        return previous;
    }
    previous.remainingFrames -= gameplayFrames;
    previous.radius = std::min(kBlastRadius, previous.radius +
                                              kBlastRadius / kBlastGrowthFrames * gameplayFrames);
    return previous;
}
}
