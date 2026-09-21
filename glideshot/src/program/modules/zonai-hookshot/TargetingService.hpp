// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Picking a wall: the camera cast, the verdict and the commit. The committed anchor is the exact
// ray hit.
#pragma once

#include "HookshotRuntime.hpp"

namespace zonai_hookshot::targeting {
// Cast past maxRange so TooFar and Miss stay distinct.
constexpr float kCastLength = pure::kTargetCastLength;

// Aim ray tangents right and up so the marker clears Link's head; the marker still sits where the
// ray hits (about 154 px right, 97 px up at 1600x900).
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
