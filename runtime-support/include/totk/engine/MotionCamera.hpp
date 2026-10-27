// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/engine/CodeCheck.hpp"
#include "totk/engine/MotionAim.hpp"
#include <cstring>

namespace totk::engine::motion {
inline bool supported{};
inline void validate(std::uintptr_t base) {
    constexpr CodeSite sites[]{
        {0xD187B0, 0x5290C5E3, 0x72B47EA3},
        {0xD18A38, 0x531E7508, 0x12196929},
        {0xD18A64, 0x2A0D1908, 0xB9003668},
        {0xD18204, 0x9110C280, 0xAA1303E1},
        {0xD184C0, 0xF841C02C, 0xB940242D},
        {0xD18580, 0xF805C008, 0xF900340A},
        {0xD18598, 0x2D428400, 0x2D070C00},
    };
    supported = codeMatches(base, sites, "motion", "LAYOUT_REFUSED");
}

// The caller has verified this tick's player-camera owner; no borrowed pointer is retained.
inline Sample sample(Frame frame) {
    constexpr std::uintptr_t componentState = 1096, optionFlags = 52;
    const auto state = frame.camera;
    const auto flags = readMemory<std::uint8_t>(state - componentState + optionFlags);
    const bool fresh = !readMemory<std::uint8_t>(state + 1221) && readMemory<std::uint8_t>(state + 1200);
    Sample input{{}, (flags & 4) != 0, fresh, (flags & 32) != 0, (flags & 64) != 0};
    if (!input.permitted || !fresh || !frame.running || frame.wheelOpen) return input;
    input.rate = targetingRate(readMemory<Vector>(state + 1080), readMemory<Vector>(state + 1140));
    return input;
}

inline void applyCamera(AimState& aim, Frame frame, float* output, Vector pivot) {
    if (!supported) return;
    const auto input = frame.aiming ? sample(frame) : Sample{};
    aim = advance(aim, frame, input);
    if (aim.angle.yaw == 0 && aim.angle.pitch == 0) return;
    View view;
    std::memcpy(&view, output, sizeof(view));
    if (const auto rotated = orbit(view, pivot, aim.angle))
        std::memcpy(output, &*rotated, sizeof(view));
}
} // namespace totk::engine::motion
