// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include "../pure/SurveySweep.hpp"

namespace survey_fidelity {
enum class Mode : unsigned { Off, Calibration, UniformCheck, RawDepth, Depth, Stripe };
constexpr Mode nextMode(Mode mode) { return Mode((unsigned(mode) + 1) % 6); }
constexpr bool usesDepth(Mode mode) { return mode >= Mode::RawDepth; }
constexpr bool usesUniforms(Mode mode) { return mode == Mode::UniformCheck || mode >= Mode::Depth; }
constexpr unsigned programIndex(Mode mode) {
    return mode == Mode::Calibration ? 0 : mode == Mode::UniformCheck ? 1 : mode == Mode::RawDepth ? 2 : 3;
}
constexpr std::uint64_t kTraceCapacity = 64 * 1024;
constexpr bool traceFits(std::uint64_t offset, std::uint64_t bytes) {
    return bytes != 0 && offset <= kTraceCapacity && bytes <= kTraceCapacity - offset;
}
constexpr unsigned depthSlot(unsigned flags) { return flags & 1 ? 0x700 : flags & 2 ? 0xe40 : 0; }
constexpr bool validDimensions(unsigned width, unsigned height) {
    return width >= 16 && height >= 16 && width <= 8192 && height <= 8192;
}
inline constexpr unsigned kBandCount = 8;
inline constexpr float kBandSpacing = 18.0f;
inline constexpr float kScanRange = 450.0f;
inline constexpr float kScanSeconds = 10.0f;
inline constexpr float kBandHalfWidth = 0.18f;
inline constexpr float kBandOpacity = 1.0f;
inline constexpr float kContourRise = 4.0f;
inline constexpr float kImprintSeconds = zonai_survey::pure::kSweepTicks / 60.0f;
inline constexpr float kGridSpacing = 3.0f;
inline constexpr float kImprintHalfWidth = 0.08f;
inline constexpr std::uint32_t kTextOnlyRingBytes = 0x32000;
struct Point { float x{}, y{}, z{}; };
inline float sfAbs(float x) { return std::abs(x); }
inline float sfSqrt(float x) { return std::sqrt(x); }
inline float sfMin(float a, float b) { return std::min(a,b); }
inline float sfMax(float a, float b) { return std::max(a,b); }
inline float sfClamp(float x, float a, float b) { return std::clamp(x,a,b); }
inline float sfFloor(float x) { return std::floor(x); }
inline float sfLog2(float x) { return std::log2(x); }
inline float sfExp2(float x) { return std::exp2(x); }
#define SF_INLINE inline
#include "../../shaders/aesthetic_math.inl"
#undef SF_INLINE
inline SfPlane sfFit(SfSample samples[9],int candidates) { return sfFit(samples,candidates,-1.f); }
inline float scanConeCosHalf() { return std::cos(zonai_survey::pure::kSurveyWidthRadians*0.5f); }
inline float imprintFront(float seconds) { return zonai_survey::pure::sweepFrontAt(seconds*60.0f); }
inline float imprintSecondsToReach(float forward) {
    return zonai_survey::pure::sweepTicksToReach(sfRingCoordinate(std::max(0.f,forward)))/60.0f;
}
inline float pulseFront(float seconds) {
    const float u = std::clamp(seconds / kScanSeconds, 0.0f, 1.0f);
    return u * (kScanRange + (kBandCount - 1) * kBandSpacing);
}
inline float trailLength(float frameSeconds) {
    const float speed = (kScanRange + (kBandCount-1)*kBandSpacing)/kScanSeconds;
    return std::clamp(speed*std::clamp(frameSeconds, 1.f/120.f, 1.f/20.f)*1.25f, 0.6f, 3.6f);
}
inline float secondsToReach(float radius) {
    float lo = 0, hi = kScanSeconds;
    for (unsigned i = 0; i < 20; ++i) {
        const float mid = (lo + hi) * 0.5f;
        if (pulseFront(mid) < radius) lo = mid; else hi = mid;
    }
    return hi;
}
inline bool validDepth(float depth) {
    return std::isfinite(depth) && depth >= 0.0f && depth < 1.0f;
}
inline float bandDistance(float radius, float front, unsigned count = kBandCount) {
    float distance = 1.0e10f;
    for (unsigned i = 0; i < count; ++i) {
        const float center = front - float(i) * kBandSpacing;
        if (center > 0.0f && center <= kScanRange)
            distance = std::min(distance, std::abs(radius - center));
    }
    return distance;
}
inline bool unprojectAt(const float* inverse, float x, float y, float z, Point& result) {
    float p[4]{};
    for (unsigned row = 0; row < 4; ++row)
        p[row] = inverse[row*4]*x + inverse[row*4+1]*y + inverse[row*4+2]*z + inverse[row*4+3];
    if (!std::isfinite(p[3]) || std::abs(p[3]) < 1e-8f) return false;
    result = {p[0]/p[3], p[1]/p[3], p[2]/p[3]};
    return std::isfinite(result.x) && std::isfinite(result.y) && std::isfinite(result.z);
}
inline bool depthRange(const float* inverse, float& nearDistance, float& farDistance) {
    Point a{}, b{};
    if (!unprojectAt(inverse, 0, 0, -1, a) || !unprojectAt(inverse, 0, 0, 1, b)) return false;
    nearDistance = -a.z; farDistance = -b.z;
    return nearDistance > 0 && farDistance > nearDistance && std::isfinite(farDistance);
}
inline bool reconstruct(const float* inverse, float u, float v, float depth, Point& result) {
    if (!validDepth(depth)) return false;
    Point a{}, b{};
    if (!unprojectAt(inverse, u*2-1, v*2-1, -1, a) ||
        !unprojectAt(inverse, u*2-1, v*2-1, 1, b)) return false;
    result = {a.x+(b.x-a.x)*depth, a.y+(b.y-a.y)*depth, a.z+(b.z-a.z)*depth};
    return std::isfinite(result.x) && std::isfinite(result.y) && std::isfinite(result.z);
}
inline bool validClipRange(float n, float f) {
    return std::isfinite(n) && std::isfinite(f) && n > 0 && f > n;
}
inline bool reconstructLinear(const float* inverse, float u, float v, float depth,
                              float n, float f, bool perspective, Point& result) {
    if (!validDepth(depth) || !validClipRange(n, f)) return false;
    Point ray{};
    if (!unprojectAt(inverse, u*2-1, v*2-1, -1, ray) || ray.z >= 0) return false;
    const float distance = n + depth*(f-n);
    const float scale = perspective ? distance / -ray.z : 1.0f;
    result = {ray.x*scale, ray.y*scale, -distance};
    return std::isfinite(result.x) && std::isfinite(result.y) && std::isfinite(result.z);
}
inline float pixelLineCoverage(float signedDistance, float halfWidth, float footprint) {
    const float span = std::clamp(footprint, 0.04f, 2.0f);
    const float width = std::max(halfWidth, 0.35f*span);
    const float lo = std::max(signedDistance-span*0.5f, -width);
    const float hi = std::min(signedDistance+span*0.5f, width);
    return std::clamp((hi-lo)/span, 0.0f, 1.0f);
}
inline float edgeWidth(float centerDepth, float neighborDepth, float gradient) {
    if (!std::isfinite(neighborDepth) || std::abs(neighborDepth-centerDepth) > 1.0f) return 0.025f;
    return std::clamp(std::abs(gradient), 0.025f, 0.20f);
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
