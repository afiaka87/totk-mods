// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <stdint.h>

namespace bivouac::placement {

enum class Lane : uint8_t { Cliff = 0, MotherBase = 1 };

constexpr Lane initialLane(bool climbIsRecent) {
    return climbIsRecent ? Lane::Cliff : Lane::MotherBase;
}

constexpr const char* laneName(Lane lane) {
    return lane == Lane::MotherBase ? "Mother Base" : "cliff";
}

constexpr int timeoutTicks(Lane lane, int cliffTicks, int motherBaseTicks) {
    return lane == Lane::MotherBase ? motherBaseTicks : cliffTicks;
}

constexpr float minimumSeparation(Lane lane, float cliffMeters, float motherBaseMeters) {
    return lane == Lane::MotherBase ? motherBaseMeters : cliffMeters;
}

constexpr bool timedOut(int tick, int armedTick, Lane lane,
                        int cliffTicks, int motherBaseTicks) {
    const int elapsed = tick - armedTick;
    return elapsed >= 0 && elapsed > timeoutTicks(lane, cliffTicks, motherBaseTicks);
}

constexpr bool wallRetriesExhausted(int attempts, int maximumAttempts) {
    return maximumAttempts > 0 && attempts >= maximumAttempts;
}

constexpr float clampSeat(float measuredSeat, float maximumSeat) {
    return measuredSeat > maximumSeat ? maximumSeat : measuredSeat;
}

constexpr float boundedGap(
    float seat, float deepestHit, float maximumGap) {
    const float gap = seat - deepestHit;
    if (gap < 0.0f) {
        return 0.0f;
    }
    return gap > maximumGap ? maximumGap : gap;
}

consteval bool contracts() {
    if (initialLane(true) != Lane::Cliff) return false;
    if (initialLane(false) != Lane::MotherBase) return false;
    if (timeoutTicks(Lane::Cliff, 160, 330) != 160) return false;
    if (timeoutTicks(Lane::MotherBase, 160, 330) != 330) return false;
    if (minimumSeparation(Lane::Cliff, 20.0f, 50.0f) != 20.0f) return false;
    if (minimumSeparation(Lane::MotherBase, 20.0f, 50.0f) != 50.0f) return false;
    if (timedOut(160, 0, Lane::Cliff, 160, 330)) return false;
    if (!timedOut(161, 0, Lane::Cliff, 160, 330)) return false;
    if (timedOut(330, 0, Lane::MotherBase, 160, 330)) return false;
    if (!timedOut(331, 0, Lane::MotherBase, 160, 330)) return false;
    if (timedOut(9, 10, Lane::Cliff, 160, 330)) return false;
    if (wallRetriesExhausted(2, 3)) return false;
    if (!wallRetriesExhausted(3, 3)) return false;
    if (clampSeat(3.0f, 2.5f) != 2.5f) return false;
    if (clampSeat(2.0f, 2.5f) != 2.0f) return false;
    if (boundedGap(1.0f, 2.0f, 5.4f) != 0.0f) return false;
    if (boundedGap(6.0f, 0.0f, 5.4f) != 5.4f) return false;
    if (boundedGap(4.0f, 1.0f, 5.4f) != 3.0f) return false;
    return true;
}

static_assert(contracts(), "Bivouac placement policy contracts must hold");

} // namespace bivouac::placement
