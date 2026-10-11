// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "CryonisCore.hpp"
namespace cryonis {
struct Pose {
    float width{}, height{}, translation{};
};
template <unsigned N>
inline float cubic(float frame, const float (&frames)[N], const float (&keys)[N][4], float scale) {
    frame = std::clamp(frame, frames[0], frames[N - 1]);
    unsigned i = 0;
    while (i + 1 < N && frames[i + 1] <= frame)
        ++i;
    const float t = i + 1 < N ? (frame - frames[i]) / (frames[i + 1] - frames[i]) : 0;
    return .505f + scale * (((keys[i][3] * t + keys[i][2]) * t + keys[i][1]) * t + keys[i][0]);
}
inline Pose originalPose(float frame) {
    // Owned Appear_Linear: quantized cubic coefficients, normalized segment time.
    constexpr float xFrames[]{0, 2, 9, 12, 100}, yFrames[]{0, 10, 22, 100};
    constexpr float xKeys[][4]{
        {-69, 8, 2, 5}, {-54, 90, 127, -98}, {65, 21, -31, 14}, {69, 0, 0, 0}, {69, 0, 0, 0}};
    constexpr float yKeys[][4]{
        {-73, 0, -8, 8}, {-73, 10, 20, -10}, {-54, 127, 0, 0}, {73, 0, 0, 0}};
    const float x = cubic(frame, xFrames, xKeys, .007202835f),
                y = cubic(frame, yFrames, yKeys, .006755905f);
    // Six identical child curves compensate parent scale before inverse binding.
    return {x, y, -.2f + .2f * y};
}
inline float collisionHeight(float frame) {
    return kHeight * std::clamp(originalPose(frame).height, 0.0f, 1.0f);
}
// The two-foot capture band is an authored choice, not a recovered BotW constant.
inline constexpr float kCaptureDepth = .6096f, kRiderClearance = .04f;
inline constexpr float kSubmergeDepth = kCaptureDepth + kRiderClearance;
inline float growthProgress(float frame) {
    const float start = originalPose(0).height, end = originalPose(100).height;
    return std::clamp((originalPose(frame).height - start) / (end - start), 0.0f, 1.0f);
}
inline float emergenceHeight(float frame) {
    const float progress = growthProgress(frame);
    return -kSubmergeDepth + (kHeight - kSettledSubmerge + kSubmergeDepth) * progress;
}
inline Vec3 visualBase(Vec3 waterBase, float frame) {
    waterBase.y -=
        kSettledSubmerge + (kSubmergeDepth - kSettledSubmerge) * (1 - growthProgress(frame));
    return waterBase;
}
inline Vec3 collisionBase(Vec3 waterBase, float frame) {
    waterBase.y += emergenceHeight(frame) - kHeight;
    return waterBase;
}
}
