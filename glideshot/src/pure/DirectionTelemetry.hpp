// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Fall-direction diagnostic: compares produced movement with the Link-to-anchor corridor; never
// alters velocity.
#pragma once

#include "Vec3.hpp"

namespace zonai_hookshot::pure {
struct DirectionTelemetry {
    bool valid = false;
    float stepDistance = 0.0f;
    float stepAlignment = 0.0f;       // 1 = directly toward the live anchor
    float signedHorizontalSine = 0.0f; // yaw-side indicator, [-1, 1]
    float lineLateral = 0.0f;         // metres from the initial start-anchor line
};

inline DirectionTelemetry measureDirection(const Vec3& start, const Vec3& target,
                                            const Vec3& previous,
                                            const Vec3& current) {
    DirectionTelemetry out{};
    const Vec3 liveToTarget = sub(target, current);
    const Vec3 step = sub(current, previous);
    const Vec3 initialToTarget = sub(target, start);
    const float liveLength = length(liveToTarget);
    const float stepLength = length(step);
    const float initialLength = length(initialToTarget);
    if (!finite3(liveToTarget) || !finite3(step) || !finite3(initialToTarget) ||
        liveLength < 1.0e-3f || stepLength < 1.0e-5f || initialLength < 1.0e-3f)
        return out;

    const Vec3 liveUnit = mul(liveToTarget, 1.0f / liveLength);
    const Vec3 initialUnit = mul(initialToTarget, 1.0f / initialLength);
    const Vec3 displacement = sub(current, start);
    const float along = dot(displacement, initialUnit);
    const Vec3 lateral = sub(displacement, mul(initialUnit, along));

    out.valid = true;
    out.stepDistance = stepLength;
    out.stepAlignment = dot(step, liveUnit) / stepLength;
    out.lineLateral = length(lateral);

    const float liveHorizontal = std::sqrt(liveToTarget.x * liveToTarget.x +
                                           liveToTarget.z * liveToTarget.z);
    const float stepHorizontal =
        std::sqrt(step.x * step.x + step.z * step.z);
    if (liveHorizontal > 1.0e-4f && stepHorizontal > 1.0e-5f) {
        out.signedHorizontalSine =
            (liveToTarget.x * step.z - liveToTarget.z * step.x) /
            (liveHorizontal * stepHorizontal);
    }
    return out;
}

}  // namespace zonai_hookshot::pure
