// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Move Link along the frozen line, then hand off to native capture.

// The optional Fall route retains its launch, velocity and Parasail handoff behavior.

// Action-thread hook bodies run on the game's action threads and talk to the tick only through DriveMailbox atomics.
#pragma once

#include "HookshotRuntime.hpp"

namespace zonai_hookshot::transport {
// Production carrier tuning.
inline constexpr pure::PositionZipConfig kPositionZipConfig{};
static_assert(kPositionZipConfig.speed == 60.0f);
static_assert(kPositionZipConfig.updateRate == 60.0f);
static_assert(kPositionZipConfig.standoff == 1.0f);

// Laboratory lane only.
constexpr float kDirectionTestArriveRadius = 0.5f;

// Freeze the path and arm Parasail admission; reject an invalid start.
bool startPositionZip(HookshotRuntime& runtime);

// One carrier step, then the endpoint hold once the standoff is reached.
void positionDriveTick(HookshotRuntime& runtime);
void holdPositionEndpoint(HookshotRuntime& runtime);

void onPositionZipEnded(HookshotRuntime& runtime);
void onPositionZipFailed(HookshotRuntime& runtime);

// Release every launch and handoff override on non-success exits.
void stopDrive(HookshotRuntime& runtime, const char* reason);

// First reason wins; a non-empty reason is the machine's transportDone signal.
void endTrip(HookshotRuntime& runtime, const char* reason);

void onDetachRequested(HookshotRuntime& runtime);
void onDetachRefused(HookshotRuntime& runtime);
void onFallEntered(HookshotRuntime& runtime);
void onTripAborted(HookshotRuntime& runtime);

void serviceFallCruise(HookshotRuntime& runtime, pure::MachineInputs& inputs);

// Obstacle probe from Link toward the anchor, stopped short of the anchor zone.
void requestCruiseProbe(HookshotRuntime& runtime);

// Native Fall action: velocity is applied after the vanilla update while a cruise is active.
void onFallEnterHook(void* action);
void onFallUpdateHook(void* action);
void onFallLeaveHook(void* action);

// Inject X into the newest live Npad sample while the launch pulse is active.
void applyLaunchInjection(void* device);

// Jump-boost consumption from the inline hook (SIMD context: no logging here).
bool consumeJumpBoost();

void reportJumpBoostEdge(HookshotRuntime& runtime);

}  // namespace zonai_hookshot::transport
