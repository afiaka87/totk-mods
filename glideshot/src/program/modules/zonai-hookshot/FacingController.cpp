// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "FacingController.hpp"

#include "HookshotLog.hpp"
#include "HookshotWorld.hpp"
#include "TransportController.hpp"

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
    return prepareYaw(drive.yaw, drive.baseRotation, desiredFacing,
                      travelDistance, runtime.yawTiming, kConfig);
}

bool startOnParasailIfReady(HookshotRuntime& runtime) {
    auto& drive = runtime.positionDrive;
    if (drive.yaw.timing != YawTiming::OnParasail || drive.yaw.started ||
        runtime.drive.parasailActive.load(std::memory_order_acquire) == 0) {
        return false;
    }
    if (!startYawOnParasail(drive.yaw, drive.path.advanced,
                            drive.path.travelDistance, kConfig)) {
        drive.failed = true;
        ZHLOG("YAW_START_REFUSED mode=GLIDER adv_dm=%d travel_dm=%d",
              (int)(drive.path.advanced * 10.0f),
              (int)(drive.path.travelDistance * 10.0f));
        return false;
    }
    ZHLOG("YAW_BEGIN mode=GLIDER adv_dm=%d duration_dm=%d angle_mdeg=%d",
          (int)(drive.path.advanced * 10.0f),
          (int)(drive.yaw.durationDistance * 10.0f),
          (int)(drive.yaw.signedRadians * 57295.7795f));
    return true;
}

void advanceOneStep(HookshotRuntime& runtime) {
    advanceYaw(runtime.positionDrive.yaw,
               transport::kPositionZipConfig.speed /
                   transport::kPositionZipConfig.updateRate);
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
