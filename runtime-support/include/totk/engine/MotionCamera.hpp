// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "totk/engine/CodeCheck.hpp"
#include "totk/engine/MotionAim.hpp"
#if defined(TOTK_PORT) && TOTK_PORT
#include "totk/port/CodeCheck.hpp"
#endif
#include <cstring>

namespace totk::engine::motion {
inline bool supported{};
// With TOTK_PORT the sites are 1.2.1 places checked against the running game; these copies have their own names.
#if defined(TOTK_PORT) && TOTK_PORT
inline namespace ported {
inline void validate(std::uintptr_t base) {
    constexpr port::Site sites[]{
        {0xD187B0, 0x5290C5E3, 0x72B47EA3},
        {0xD18A38, 0x531E7508, 0x12196929},
        {0xD18A64, 0x2A0D1908, 0xB9003668},
        {0xD18204, 0x9110C280, 0xAA1303E1},
        {0xD184C0, 0xF841C02C, 0xB940242D},
        {0xD18580, 0xF805C008, 0xF900340A},
        {0xD18598, 0x2D428400, 0x2D070C00},
    };
    supported = port::codeMatches(base, sites, "motion", "LAYOUT_REFUSED");
}
inline std::uintptr_t componentState() { return port::pick<std::uintptr_t>(1096, 1168); }
#else
inline std::uintptr_t stateInComponent = 1096;
// Without TOTK_PORT the caller names the game. The 1.4.3 sites, read from its image, are 1.2.1's same accesses.
inline void validate(std::uintptr_t base, bool game143) {
    constexpr CodeSite v121[]{
        {0xD187B0, 0x5290C5E3, 0x72B47EA3},
        {0xD18A38, 0x531E7508, 0x12196929},
        {0xD18A64, 0x2A0D1908, 0xB9003668},
        {0xD18204, 0x9110C280, 0xAA1303E1},
        {0xD184C0, 0xF841C02C, 0xB940242D},
        {0xD18580, 0xF805C008, 0xF900340A},
        {0xD18598, 0x2D428400, 0x2D070C00},
    };
    constexpr CodeSite v143[]{
        {0x875258, 0x5290C5E3, 0x72B47EA3},
        {0x875428, 0x12196929, 0xBD400000},
        {0x87545C, 0x2A091908, 0xB9003668},
        {0x874D84, 0x9110C280, 0xAA1303E1},
        {0x874F9C, 0xF841C02C, 0xB940242D},
        {0x87505C, 0xF805C008, 0xF900340A},
        {0x875074, 0x2D428400, 0x2D070C00},
    };
    stateInComponent = game143 ? 1168 : 1096;
    supported = game143 ? codeMatches(base, v143, "motion", "LAYOUT_REFUSED")
                        : codeMatches(base, v121, "motion", "LAYOUT_REFUSED");
}
inline void validate(std::uintptr_t base) { validate(base, false); }
inline std::uintptr_t componentState() { return stateInComponent; }
#endif

// The caller has verified this tick's player-camera owner; no borrowed pointer is retained.
inline Sample sample(Frame frame) {
    constexpr std::uintptr_t optionFlags = 52;
    const auto state = frame.camera;
    const auto flags = readMemory<std::uint8_t>(state - componentState() + optionFlags);
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
#if defined(TOTK_PORT) && TOTK_PORT
}  // inline namespace ported
#endif
} // namespace totk::engine::motion
