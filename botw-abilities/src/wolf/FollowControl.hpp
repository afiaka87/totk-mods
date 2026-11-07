// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>

namespace botw_abilities::wolf {

enum class FollowStatus : std::uint8_t { Running, Succeeded, Failed };

struct CliffEscapeCount {
    std::uint8_t attempts{};
    bool wander{}, gaveUp{};
};

// After the third escape the original wanders, or gives up when the player is over thirty units
// away. It compares the incremented byte as signed, including wraparound.
[[nodiscard]] constexpr CliffEscapeCount countCliffEscape(std::uint8_t attempts, bool failed, float leaderDistanceSquared) {
    ++attempts;
    if (attempts < 3 || attempts >= 128) return {attempts, false, failed};
    return {attempts, true, failed || !(leaderDistanceSquared <= 900.0f)};
}

}
