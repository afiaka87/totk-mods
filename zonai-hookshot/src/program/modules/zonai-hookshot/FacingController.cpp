// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "FacingController.hpp"

#include "HookshotLog.hpp"
#include "HookshotWorld.hpp"
#include "TravelFacing.hpp"

namespace zonai_hookshot::facing {
using namespace zonai_hookshot::pure;

bool captureBasis(HookshotRuntime& runtime) {
    float rotation[9]{};
    if (!world::readPlayerRotation(rotation)) return false;
    for (int i = 0; i < 9; ++i) {
        if (!__builtin_isfinite(rotation[i])) return false;
        runtime.positionDrive.baseRotation[i] = rotation[i];
    }
    return true;
}

bool prepare(HookshotRuntime& runtime, const Vec3& desiredFacing,
             float travelDistance) {
    auto& drive = runtime.positionDrive;
    drive.desiredFacing = desiredFacing;
    return prepareTravelFacing(drive.yaw, drive.baseRotation, drive.path.direction,
                               desiredFacing, travelDistance);
}

void updateProgress(HookshotRuntime& runtime) {
    updateTravelFacing(runtime.positionDrive.yaw,runtime.positionDrive.path.advanced);
}

bool applyPose(HookshotRuntime& runtime, const Vec3& position) {
    float rotation[9]{};
    if (!easedYawBasis(runtime.positionDrive.baseRotation,
                       runtime.positionDrive.yaw, rotation)) {
        return false;
    }
    // Only an invalid basis refuses the carrier; the write's result is not a drive verdict.
    (void)world::forcePlayerPose(rotation, position);
    return true;
}

int facingErrorMilliDegrees(const HookshotRuntime& runtime) {
    float rotation[9]{};
    if (!world::readPlayerRotation(rotation)) return -1;
    float error = 0.0f;
    if (!signedHorizontalYaw({rotation[2], 0.0f, rotation[8]},
                             runtime.positionDrive.desiredFacing, error)) {
        return -1;
    }
    if (error < 0.0f) error = -error;
    return (int)(error * 57295.7795f);
}

}  // namespace zonai_hookshot::facing
