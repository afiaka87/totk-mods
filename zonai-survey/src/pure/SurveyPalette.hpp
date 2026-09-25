// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#pragma once

#include <cmath>
#include <cstdint>

#include "ScanReduction.hpp"
#include "SurveySweep.hpp"

namespace zonai_survey::pure {

struct SurveyRgb {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

struct SurveyRgba {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

constexpr float clampUnit(float value) {
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

constexpr SurveyRgb mixColor(const SurveyRgb& a, const SurveyRgb& b,
                             float amount) {
    const float t = clampUnit(amount);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t};
}

constexpr SurveyRgba mixColor(const SurveyRgba& a, const SurveyRgba& b,
                              float amount) {
    const float t = clampUnit(amount);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

inline constexpr SurveyRgba kLaneStart[kSlopeLaneCount] = {
    {0.10f, 0.95f, 1.00f, 0.35f},
    {0.95f, 0.75f, 0.00f, 0.35f},
};
inline constexpr SurveyRgba kLaneEnd[kSlopeLaneCount] = {
    {0.90f, 0.87f, 0.00f, 0.35f},
    {1.00f, 0.22f, 0.22f, 0.35f},
};

constexpr const SurveyRgba& laneStartColor(std::uint32_t lane) {
    return kLaneStart[lane];
}

constexpr const SurveyRgba& laneEndColor(std::uint32_t lane) {
    return kLaneEnd[lane];
}

struct SlopeStop {
    SlopeLane lane = SlopeLane::Cool;
    float rate = 0.0f;
};

inline constexpr SlopeStop kSlopeStops[kSlopeBandCount] = {
    {SlopeLane::Cool, 0.00f},
    {SlopeLane::Cool, 0.42f},
    {SlopeLane::Cool, 1.00f},
    {SlopeLane::Warm, 0.00f},
    {SlopeLane::Warm, 0.50f},
    {SlopeLane::Warm, 1.00f},
};

constexpr std::uint32_t bandIndex(SlopeBand band) {
    const std::uint32_t index = static_cast<std::uint32_t>(band);
    return index < kSlopeBandCount ? index : kSlopeBandCount - 1u;
}

constexpr SlopeLane slopeLane(SlopeBand band) {
    return kSlopeStops[bandIndex(band)].lane;
}

constexpr float slopeRate(SlopeBand band) {
    return kSlopeStops[bandIndex(band)].rate;
}

inline constexpr float kSlopeBlendHalfWidth = 0.06f;

constexpr float continuousSlopeRate(float steepness, SlopeBand band) {
    if (!(steepness >= 0.0f)) return slopeRate(band);
    struct BandJoin {
        float threshold;
        SlopeBand low;
        SlopeBand high;
    };
    constexpr BandJoin kJoins[] = {
        {0.208f, SlopeBand::Level, SlopeBand::Rolling},
        {0.407f, SlopeBand::Rolling, SlopeBand::Inclined},
        {0.766f, SlopeBand::Steep, SlopeBand::Severe},
        {0.940f, SlopeBand::Severe, SlopeBand::Vertical},
    };
    for (const BandJoin& join : kJoins) {
        const float lo = join.threshold - kSlopeBlendHalfWidth;
        const float hi = join.threshold + kSlopeBlendHalfWidth;
        if (steepness < lo || steepness > hi) continue;
        const float t = (steepness - lo) / (hi - lo);
        const float a = slopeRate(join.low);
        const float b = slopeRate(join.high);
        return a + (b - a) * t;
    }
    return slopeRate(band);
}

constexpr SurveyRgba slopeColorRgba(SlopeBand band) {
    const std::uint32_t lane = laneIndex(slopeLane(band));
    return mixColor(laneStartColor(lane), laneEndColor(lane), slopeRate(band));
}

constexpr SurveyRgb slopeColor(SlopeBand band) {
    const SurveyRgba color = slopeColorRgba(band);
    return {color.r, color.g, color.b};
}

inline constexpr SurveyRgb kSurveyPearl{0.88f, 0.93f, 0.97f};

inline float surveyRangeProgress(float revealWave) {
    if (!std::isfinite(revealWave)) return 1.0f;
    return clampUnit((revealWave - 1.0f) /
                     static_cast<float>(kRings - 1u));
}

inline constexpr float kSurveyNearPearl = 0.16f;
inline constexpr float kSurveyNearPearlEnd = 0.20f;

inline float surveyRangePearl(float revealWave) {
    const float progress = surveyRangeProgress(revealWave);
    if (progress >= kSurveyNearPearlEnd) return 0.0f;
    return kSurveyNearPearl * (1.0f - progress / kSurveyNearPearlEnd);
}

inline SurveyRgba surveyLaneColor(SlopeLane lane, bool end, float revealWave) {
    const std::uint32_t index = laneIndex(lane);
    const SurveyRgba base = end ? laneEndColor(index) : laneStartColor(index);
    const SurveyRgb tinted = mixColor(SurveyRgb{base.r, base.g, base.b},
                                      kSurveyPearl, surveyRangePearl(revealWave));
    return {tinted.r, tinted.g, tinted.b, base.a};
}

inline SurveyRgba surveyBandColor(SlopeBand band, float revealWave) {
    const SurveyRgba base = slopeColorRgba(band);
    const SurveyRgb tinted = mixColor(SurveyRgb{base.r, base.g, base.b},
                                      kSurveyPearl, surveyRangePearl(revealWave));
    return {tinted.r, tinted.g, tinted.b, base.a};
}

inline SurveyRgb surveyCoreColor(SlopeBand band, float revealWave) {
    const SurveyRgba color = surveyBandColor(band, revealWave);
    return {color.r, color.g, color.b};
}

constexpr SlopeLane surveyLaneFor(const ScanSegment& segment) {
    return slopeLane(segment.slopeBand);
}

inline SurveyRgba surveySegmentColor(const ScanSegment& segment,
                                     float revealWave) {
    const SlopeLane lane = slopeLane(segment.slopeBand);
    return mixColor(surveyLaneColor(lane, false, revealWave),
                    surveyLaneColor(lane, true, revealWave),
                    continuousSlopeRate(segment.steepness, segment.slopeBand));
}

}
