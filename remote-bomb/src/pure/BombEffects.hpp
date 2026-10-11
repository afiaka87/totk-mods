// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "BombState.hpp"
#include <array>
#include <cmath>

// The appearance burst drawn where a bomb comes out, and which forms have a live bomb.
namespace remote_bomb::effects {
using Position = std::array<float, 3>;
inline constexpr float kBurstFrames = 20.f;
// The burst sits 0.3 m above the bomb's origin (the original ELink offset).
inline constexpr float kBurstHeight = .3f;
struct Burst {
    Position position{};
    Ticket ticket{};
    float frames{};
    bool active{};
};
struct Timeline {
    std::array<Burst, 2> bursts{};
    std::array<bool, 2> live{};
};
inline bool finite(Position position) {
    return std::isfinite(position[0]) && std::isfinite(position[1]) && std::isfinite(position[2]);
}
inline Timeline begin(Timeline previous, Shape shape, Position position, Ticket ticket) {
    auto &burst = previous.bursts[index(shape)];
    if (!finite(position) || !ticket.scene || !ticket.serial || burst.ticket == ticket)
        return previous;
    position[1] += kBurstHeight;
    burst = {position, ticket, 0.f, true};
    return previous;
}
inline Timeline advance(Timeline previous, float gameplayFrames) {
    if (!std::isfinite(gameplayFrames) || gameplayFrames <= 0.f)
        return previous;
    for (auto &burst : previous.bursts)
        if (burst.active && (burst.frames += gameplayFrames) >= kBurstFrames)
            burst.active = false;
    return previous;
}
// A live bomb's burst follows it until the burst ends.
inline Timeline track(Timeline previous, Shape shape, Ticket ticket, Position position, bool live) {
    const auto form = index(shape);
    previous.live[form] = live && finite(position);
    auto &burst = previous.bursts[form];
    if (previous.live[form] && burst.active && burst.ticket == ticket) {
        burst.position = position;
        burst.position[1] += kBurstHeight;
    }
    return previous;
}
inline bool visible(const Timeline &timeline) {
    for (unsigned form = 0; form < 2; ++form)
        if (timeline.bursts[form].active || timeline.live[form])
            return true;
    return false;
}
}
