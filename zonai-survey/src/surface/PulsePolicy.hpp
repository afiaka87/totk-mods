// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include "../pure/SurveySweep.hpp"

namespace zonai_survey::pulse {
constexpr unsigned depthSlot(unsigned flags, unsigned full, unsigned half) {
    return flags & 1 ? full : flags & 2 ? half : 0;
}
constexpr unsigned depthSlot(unsigned flags) { return depthSlot(flags, 0x700, 0xe40); }
constexpr bool validDimensions(unsigned width, unsigned height) {
    return width >= 16 && height >= 16 && width <= 8192 && height <= 8192;
}
inline constexpr unsigned kFitCandidates = 8;
inline constexpr float kScanOpacity = 1.0f;
inline constexpr float kImprintSeconds = zonai_survey::pure::kSweepTicks / 60.0f;
inline constexpr float kGridSpacing = 3.0f;
inline constexpr float kImprintHalfWidth = 0.08f;
inline float sfAbs(float x) { return std::abs(x); }
inline float sfSqrt(float x) { return std::sqrt(x); }
inline float sfMin(float a, float b) { return std::min(a,b); }
inline float sfMax(float a, float b) { return std::max(a,b); }
inline float sfClamp(float x, float a, float b) { return std::clamp(x,a,b); }
inline float sfFloor(float x) { return std::floor(x); }
inline float sfLog2(float x) { return std::log2(x); }
inline float sfExp2(float x) { return std::exp2(x); }
// The shader's math is shared verbatim so host tests exercise the same code the GPU runs.
#define SF_INLINE inline
#include "../../shaders/aesthetic_math.inl"
#undef SF_INLINE
inline SfPlane sfFit(SfSample samples[9],int candidates) { return sfFit(samples,candidates,-1.f); }
inline float scanConeCosHalf() { return std::cos(zonai_survey::pure::kSurveyWidthRadians*0.5f); }
inline float imprintFront(float seconds) { return zonai_survey::pure::sweepFrontAt(seconds*60.0f); }
inline float imprintSecondsToReach(float forward) {
    return zonai_survey::pure::sweepTicksToReach(sfRingCoordinate(std::max(0.f,forward)))/60.0f;
}
inline bool validClipRange(float n, float f) {
    return std::isfinite(n) && std::isfinite(f) && n > 0 && f > n;
}
inline bool inverse4(const float* input, float* output) {
    double a[4][8]{};
    for (unsigned r = 0; r < 4; ++r) {
        for (unsigned c = 0; c < 4; ++c) {
            if (!std::isfinite(input[r * 4 + c])) return false;
            a[r][c] = input[r * 4 + c];
        }
        a[r][r + 4] = 1;
    }
    for (unsigned col = 0; col < 4; ++col) {
        unsigned pivot = col;
        for (unsigned r = col + 1; r < 4; ++r)
            if (std::abs(a[r][col]) > std::abs(a[pivot][col])) pivot = r;
        if (std::abs(a[pivot][col]) < 1e-12) return false;
        for (unsigned c = 0; c < 8; ++c) {
            const auto temp = a[col][c]; a[col][c] = a[pivot][c]; a[pivot][c] = temp;
        }
        const auto divisor = a[col][col];
        for (auto& x : a[col]) x /= divisor;
        for (unsigned r = 0; r < 4; ++r) if (r != col) {
            const auto factor = a[r][col];
            for (unsigned c = 0; c < 8; ++c) a[r][c] -= factor * a[col][c];
        }
    }
    for (unsigned r = 0; r < 4; ++r) for (unsigned c = 0; c < 4; ++c) {
        const float value = float(a[r][c + 4]);
        if (!std::isfinite(value)) return false;
        output[r * 4 + c] = value;
    }
    return true;
}
struct alignas(16) Uniforms {
    float inverseProjection[16]{};
    float inverseView[12]{};
    float settings[4]{};
    float dimensions[4]{};
    float scanOrigin[4]{};
    float scanHeading[4]{};
    float scanStyle[4]{};
    float surfaceStyle[4]{};
    float scanMotion[4]{};
};
static_assert(sizeof(Uniforms) == 224);
}
