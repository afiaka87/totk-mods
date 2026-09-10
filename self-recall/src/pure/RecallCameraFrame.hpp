#pragma once
#include <cmath>

namespace self_recall::pure {
struct CameraVerticalCorrection {
    float cachedRootDelta = 0;
    float transitionDelta = 0;
    float appliedDelta = 0;
};

inline bool alignCameraHeight(float* camera, float presentedRootY, float cachedRootY,
                              float idealLookY, CameraVerticalCorrection& correction) {
    if (!camera || !std::isfinite(presentedRootY) || !std::isfinite(cachedRootY) ||
        !std::isfinite(idealLookY)) return false;
    for (unsigned i = 0; i < 10; ++i)
        if (!std::isfinite(camera[i])) return false;
    const float rootDelta = presentedRootY - cachedRootY;
    const float transitionDelta = idealLookY - camera[4];
    const float targetY = idealLookY + rootDelta;
    const float delta = targetY - camera[4];
    const float eyeY = camera[1] + delta;
    if (!std::isfinite(rootDelta) || !std::isfinite(transitionDelta) ||
        !std::isfinite(targetY) || !std::isfinite(delta) || !std::isfinite(eyeY)) return false;
    camera[1] = eyeY;
    camera[4] = targetY;
    correction = {rootDelta, transitionDelta, delta};
    return true;
}
} // namespace self_recall::pure
