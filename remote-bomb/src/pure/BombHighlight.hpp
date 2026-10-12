// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <array>
#include <cmath>
#include <cstdint>

// The mod's own Ultrahand highlight: the game never highlights the bombs, so the renderer draws vanilla's orange (in reach) and green (aimed at or held) itself.
namespace remote_bomb::highlight {
enum class Level : unsigned { None, InReach, Focused };
struct Inputs {
    bool active{}, focused{};
    std::uint64_t receiverFlags{};
    float distance{};
};
// An assumed reach; the game decides its own in code that the mod does not read.
constexpr float kReach = 20.f;
// Receiver state bits set only while Ultrahand holds the bomb.
constexpr std::uint64_t kHeldFlags = 0x4000 | 0x8000 | 0x10000;
inline bool held(std::uint64_t receiverFlags) { return (receiverFlags & kHeldFlags) != 0; }
inline Level decide(const Inputs &in) {
    if (held(in.receiverFlags) || (in.active && in.focused))
        return Level::Focused;
    if (in.active && std::isfinite(in.distance) && in.distance <= kReach)
        return Level::InReach;
    return Level::None;
}
// Linear colours of vanilla's outlines, sampled from screenshots: orange sRGB 253,163,2 on a rock in reach, green sRGB 54,240,124 on a held apple.
inline std::array<float, 3> colour(Level level) {
    switch (level) {
    case Level::InReach: return {.983f, .366f, .001f};
    case Level::Focused: return {.037f, .871f, .202f};
    default: return {};
    }
}
}
