// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Start facing along the route, then ease world-up yaw into the wall by 90% travel.
#pragma once

#include "HookshotRuntime.hpp"

namespace zonai_hookshot::facing {

// False when the live rotation is unreadable.
bool captureBasis(HookshotRuntime& runtime);

// False refuses the whole zip.
bool prepare(HookshotRuntime& runtime, const pure::Vec3& desiredFacing,
             float travelDistance);

void updateProgress(HookshotRuntime& runtime);

// False means the basis went invalid and the drive must fail.
bool applyPose(HookshotRuntime& runtime, const pure::Vec3& position);

// Facing error in milli-degrees; -1 when unmeasurable.
int facingErrorMilliDegrees(const HookshotRuntime& runtime);

}  // namespace zonai_hookshot::facing
