// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Validate camera hits and freeze the exact committed anchor.
#pragma once

#include "HookshotRuntime.hpp"

namespace zonai_hookshot::targeting {
// Cast past maxRange so TooFar and Miss stay distinct.
constexpr float kCastLength = pure::kTargetCastLength;

// Offset the ray above/right of Link; the marker remains at the resulting hit.
constexpr float kAimRightOffset = 0.16f;
constexpr float kAimUpOffset = 0.10f;
// Two starved aim rays while aiming means the world stopped answering.
constexpr int kStarveResetLimit = 2;

// Chain origin: Link centre at torso height.
constexpr float kOriginUp = 1.05f;

pure::Vec3 chainOrigin();

// Consume a ready ray result (or a timeout) into runtime.aim.
void serviceAimRay(HookshotRuntime& runtime);

// Issue the next camera cast while aiming.
void requestAimRay(HookshotRuntime& runtime);

const char* refusalText(pure::Verdict verdict);

void onTargetingEntered(HookshotRuntime& runtime);
void onArmingAbandoned(HookshotRuntime& runtime);
void onConfirmStarted(HookshotRuntime& runtime);
void onCommitted(HookshotRuntime& runtime);
void onRefused(HookshotRuntime& runtime);
void onLatched(HookshotRuntime& runtime);

}  // namespace zonai_hookshot::targeting
