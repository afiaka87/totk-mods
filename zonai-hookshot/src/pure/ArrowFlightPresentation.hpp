// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <algorithm>
#include <ArrowHookshot.hpp>
#include "HandheldPose.hpp"

namespace zonai_hookshot::pure {
struct ArrowFlightPresentation {
    bool anchor{};
    float animationWeight{};
    bool alignModels{};
};
inline ArrowFlightPresentation arrowFlightPresentation(arrowbound::pure::ArrowPhase phase,
                                                       bool parasail, bool climbing,
                                                       arrowbound::pure::Vec3 velocity) {
    if (phase != arrowbound::pure::ArrowPhase::Following || !parasail || climbing ||
        !arrowbound::pure::finite3(velocity)) return {};
    const float speed = arrowbound::pure::length(velocity);
    if (!std::isfinite(speed)) return {};
    // Fade toward animation as flight approaches the native 90 m/s body limit.
    const float t = std::clamp((speed - 45.f) / 45.f, 0.f, 1.f);
    return {true, t * t * (3.f - 2.f * t), t == 1.f};
}
inline float arrowAnimationWeight(float primary, float secondary, float requested) {
    const float native = primary == 1.f ? secondary : 1.f - primary;
    if (!std::isfinite(native) || !std::isfinite(requested)) return native;
    return std::max(native, std::clamp(requested, 0.f, 1.f));
}
inline bool arrowModelTranslation(const handheld::Matrix& bodyRoot,
                                  Vec3 target, Vec3& shift,
                                  float limit = arrowbound::pure::ArrowPredictionBudget::kMaxDistance) {
    for (float value : bodyRoot.v)
        if (!std::isfinite(value)) return false;
    if (!finite3(target)) return false;
    const auto delta = sub(target, handheld::position(bodyRoot));
    if (!finite3(delta) || !std::isfinite(limit) || limit < 0 || length(delta) > limit)
        return false;
    shift = delta;
    return true;
}
inline float arrowRenderLimit(Vec3 velocity) {
    if (!finite3(velocity)) return 0;
    const float speed = length(velocity);
    if (!std::isfinite(speed)) return 0;
    // Allow the live model's frame advance as well as the carrier prediction budget.
    return arrowbound::pure::ArrowPredictionBudget::kMaxDistance +
           speed * arrowbound::pure::ArrowPredictionBudget::kMaxSeconds;
}
inline bool arrowCameraFocus(Vec3 nativeFocus, Vec3 player, Vec3 target,
                             Vec3 velocity, Vec3& focus) {
    if (!finite3(nativeFocus) || !finite3(player) || !finite3(target)) return false;
    const auto shift = sub(target, player);
    const float limit = arrowRenderLimit(velocity);
    if (!(limit > 0) || length(shift) > limit) return false;
    const auto candidate = add(nativeFocus, shift);
    if (!finite3(candidate)) return false;
    focus = candidate;
    return true;
}
inline handheld::Matrix translateArrowModel(handheld::Matrix root, Vec3 shift) {
    root.v[3] += shift.x;
    root.v[7] += shift.y;
    root.v[11] += shift.z;
    return root;
}
inline bool arrowAttachmentRoot(const handheld::Matrix& oldBone,
                                 const handheld::Matrix& newBone,
                                 const handheld::Matrix& root, handheld::Matrix& result) {
    handheld::Matrix inverse{};
    for (const auto* matrix : {&oldBone, &newBone, &root})
        for (float value : matrix->v) if (!std::isfinite(value)) return false;
    if (!handheld::inverseAffine(oldBone, inverse)) return false;
    const auto candidate = handheld::compose(newBone, handheld::compose(inverse, root));
    for (float value : candidate.v) if (!std::isfinite(value)) return false;
    result = candidate;
    return true;
}
inline bool arrowRenderTranslation(const handheld::Matrix& bodyRoot, Vec3 arrow,
                                   Vec3 velocity, Vec3& target, Vec3& shift) {
    Vec3 candidate{}, delta{};
    if (!arrowbound::pure::arrowTrailPoint(arrow, velocity, candidate) ||
        !arrowModelTranslation(bodyRoot, candidate, delta, arrowRenderLimit(velocity))) return false;
    target = candidate;
    shift = delta;
    return true;
}
}
