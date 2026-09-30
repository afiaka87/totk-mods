// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "HookshotState.hpp"
#include "PositionZip.hpp"

namespace zonai_hookshot::pure {
struct TravelGlideBlend {
    float weight{}, directionDegrees{};
};

inline TravelGlideBlend travelGlideBlend(Phase phase, bool parasail, bool climbing,
                                        const PositionZipState& path, Vec3 forward) {
    if (phase != Phase::PositionCruise || !parasail || climbing || !path.active ||
        !std::isfinite(path.advanced) || !std::isfinite(path.travelDistance) ||
        path.travelDistance <= 0 || path.advanced < 0 || path.advanced >= path.travelDistance ||
        !finite3(path.direction) || !finite3(forward)) return {};
    float radians{};
    // Vertical pulls have no horizontal direction: use the native forward pose.
    if (length(Vec3{path.direction.x,0,path.direction.z}) >= 0.05f &&
        !signedHorizontalYaw(forward,path.direction,radians)) return {};
    // Presentation tuning: full stick, with at most six metres of easing per end.
    const float ramp = std::fmin(6.f,path.travelDistance * 0.15f);
    const float t = clamp01(std::fmin(path.advanced,path.travelDistance-path.advanced)/ramp);
    return {t*t*(3.f-2.f*t), radians*57.2957795f};
}

inline float glideAnimationValue(unsigned index, unsigned magnitudeIndex, unsigned directionIndex,
                                 float nativeValue, TravelGlideBlend blend) {
    if (!std::isfinite(nativeValue) || !std::isfinite(blend.weight) ||
        !std::isfinite(blend.directionDegrees) || blend.weight <= 0) return nativeValue;
    const float weight = clamp01(blend.weight);
    if (index == magnitudeIndex) return nativeValue + (1.f-nativeValue)*weight;
    if (index != directionIndex) return nativeValue;
    // The native parameter's name says Rad, but its setter and AS graph use degrees.
    const float delta = std::remainder(blend.directionDegrees-nativeValue,360.f);
    return std::remainder(nativeValue+delta*weight,360.f);
}
}
