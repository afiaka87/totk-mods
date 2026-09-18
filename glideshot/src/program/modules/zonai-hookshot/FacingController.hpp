// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Which way Link faces during the carrier: the activation basis is captured once and only its
// world-up yaw is eased, starting at the Parasail observation.
#pragma once

#include "HookshotRuntime.hpp"

namespace zonai_hookshot::facing {
// Tuning frozen by the v0.3.9 boot.
inline constexpr pure::YawConfig kConfig{};

// False when the live rotation is unreadable.
bool captureBasis(HookshotRuntime& runtime);

// False refuses the whole zip.
bool prepare(HookshotRuntime& runtime, const pure::Vec3& desiredFacing,
             float travelDistance);

// True when the turn started on this tick; the caller then skips the ordinary advance.
bool startOnParasailIfReady(HookshotRuntime& runtime);

void advanceOneStep(HookshotRuntime& runtime);

// False means the basis went invalid and the drive must fail.
bool applyPose(HookshotRuntime& runtime, const pure::Vec3& position);

// Facing error in milli-degrees; -1 when unmeasurable.
int facingErrorMilliDegrees(const HookshotRuntime& runtime);

}  // namespace zonai_hookshot::facing
