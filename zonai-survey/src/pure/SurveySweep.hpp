// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#pragma once

#include <cmath>
#include <cstdint>

#include "PulseLattice.hpp"

namespace zonai_survey::pure {

inline constexpr float kSweepBandRings = 14.0f;

inline constexpr std::uint32_t kSweepTicks = 192;

inline constexpr float kSweepEase = 0.55f;
static_assert(kSweepEase < 1.0f, "the sweep front must never reverse");

inline constexpr float kTwoPi = 2.0f * kPi;

inline float sweepEase(float u) {
    if (!(u > 0.0f)) return 0.0f;
    if (u >= 1.0f) return 1.0f + (1.0f + kSweepEase) * (u - 1.0f);
    return u + kSweepEase * std::sin(kTwoPi * u) / kTwoPi;
}

inline constexpr float kBirthSlow = 0.15f;
static_assert(kBirthSlow > 0.0f && kBirthSlow < 1.0f,
              "the warp must stay monotone: a zero start would stall the front "
              "and a value past 1 would make it decelerate into the picture");

inline float sweepBirthWarp(float u) {
    if (!(u > 0.0f)) return 0.0f;
    if (u >= 1.0f) return 1.0f + (2.0f - kBirthSlow) * (u - 1.0f);
    return u * (kBirthSlow + (1.0f - kBirthSlow) * u);
}

inline float sweepBirthWarpInverse(float w) {
    if (!(w > 0.0f)) return 0.0f;
    if (w >= 1.0f) return 1.0f + (w - 1.0f) / (2.0f - kBirthSlow);
    const float a = 1.0f - kBirthSlow;
    const float disc = kBirthSlow * kBirthSlow + 4.0f * a * w;
    return (std::sqrt(disc) - kBirthSlow) / (2.0f * a);
}

inline float sweepFrontAt(float tick) {
    if (!(tick > 0.0f)) return 0.0f;
    const float span = static_cast<float>(kRings) + kSweepBandRings;
    const float u = tick / static_cast<float>(kSweepTicks);
    return span * sweepEase(sweepBirthWarp(u));
}

inline float sweepTicksToReach(float waveCoord) {
    if (!(waveCoord > 0.0f)) return 0.0f;
    const float span = static_cast<float>(kRings) + kSweepBandRings;
    const float target = waveCoord / span;
    float eased = 0.0f;
    if (target >= 1.0f) {
        eased = 1.0f + (target - 1.0f) / (1.0f + kSweepEase);
    } else {
        float lo = 0.0f;
        float hi = 1.0f;
        for (int i = 0; i < 24; ++i) {
            const float mid = 0.5f * (lo + hi);
            if (sweepEase(mid) < target) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        eased = 0.5f * (lo + hi);
    }
    return sweepBirthWarpInverse(eased) * static_cast<float>(kSweepTicks);
}

}
