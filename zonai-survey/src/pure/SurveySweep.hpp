// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#pragma once

#include <cmath>
#include <cstdint>

#include "ScanReduction.hpp"

namespace zonai_survey::pure {

inline constexpr std::uint32_t kRevealSteps = 4;

inline constexpr float kSweepBandRings = 14.0f;

inline constexpr std::uint32_t kSweepTicks = 192;

inline constexpr float kSweepEase = 0.55f;
static_assert(kSweepEase < 1.0f, "the sweep front must never reverse");

inline constexpr std::uint32_t kSweepEndTicks = kSweepTicks;

inline constexpr float kSweepBodyFraction = 0.55f;

inline constexpr std::uint32_t kSweepLifetimeTicks = kSweepEndTicks + 60;
static_assert(kSweepLifetimeTicks < kScanLifetimeTicks,
              "the depth drawer gates on the frame lifetime; the sweep must end "
              "inside it");

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

inline float sweepFront(std::uint32_t tick) {
    return sweepFrontAt(static_cast<float>(tick));
}

inline float sweepBehind(float revealWave, std::uint32_t tick) {
    return sweepFront(tick) - revealWave;
}

inline float sweepBodyAlpha(float behind) {
    if (behind < 0.0f) return 0.0f;
    if (behind >= kSweepBandRings) return 0.0f;
    const float bodyEnd = kSweepBandRings * kSweepBodyFraction;
    if (behind <= bodyEnd) return 1.0f;
    const float t = (behind - bodyEnd) / (kSweepBandRings - bodyEnd);
    const float inverted = 1.0f - (t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t));
    return inverted * inverted * (3.0f - 2.0f * inverted);
}

inline constexpr float kSurveyWidthGain = 1.60f;

inline constexpr float kSurveyBodyBrightness = 0.90f;

inline float sweepCrest(float revealWave, std::uint32_t tick) {
    const float behind = sweepBehind(revealWave, tick);
    if (behind < 0.0f) return 0.0f;
    const float x = behind / kCrestWidthRings;
    return std::exp(-x * x);
}

inline float sweepAlpha(float revealWave, std::uint32_t tick) {
    const float body = sweepBodyAlpha(sweepBehind(revealWave, tick));
    if (body <= 0.0f) return 0.0f;
    const float crest = sweepCrest(revealWave, tick);
    return body * (kSurveyBodyBrightness +
                   (1.0f - kSurveyBodyBrightness) * crest);
}

inline constexpr float kSurveyStandoffMeters = 4.0f * kGroundClearanceMeters;

inline constexpr float kSurveyCrestLiftMeters = kCrestLiftMeters;

inline constexpr float kSurveyDepthLiftFraction = 0.01f;
static_assert(kSurveyDepthLiftFraction > 0.0f,
              "a line drawn in the surface it measured is hidden by it");

inline float sweepLift(float revealWave, std::uint32_t tick) {
    return kSurveyStandoffMeters +
           kSurveyCrestLiftMeters * sweepCrest(revealWave, tick);
}

inline bool sweepExpired(std::uint32_t tick) {
    return tick >= kSweepLifetimeTicks;
}

inline constexpr float kCrestHoldLevel = 1.0f;
inline constexpr std::uint32_t kCrestFadeTicks = 45;
static_assert(kSweepEndTicks + kCrestFadeTicks < kSweepLifetimeTicks,
              "the crest fade must finish before the scan tears down, or the "
              "ridges would vanish mid-fade");

inline float sweepAfterglow(float behind, std::uint32_t tick) {
    if (behind < 0.0f) return 0.0f;
    if (tick <= kSweepEndTicks) return kCrestHoldLevel;
    const float t = static_cast<float>(tick - kSweepEndTicks) /
                    static_cast<float>(kCrestFadeTicks);
    if (t >= 1.0f) return 0.0f;
    const float inverted = 1.0f - t;
    return kCrestHoldLevel * inverted * inverted * (3.0f - 2.0f * inverted);
}

inline float sweepCrestlineAlpha(float revealWave, std::uint32_t tick) {
    const float band = sweepAlpha(revealWave, tick);
    const float glow = sweepAfterglow(sweepBehind(revealWave, tick), tick);
    return band > glow ? band : glow;
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
