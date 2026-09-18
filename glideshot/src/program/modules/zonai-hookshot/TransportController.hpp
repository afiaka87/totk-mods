// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// How Link reaches the anchor. Production: an exact forceSetMatrix line drive at 60 m/s to a half- metre standoff
// with the paraglider admitted underneath.

// Laboratory (positionDriveEnabled false): synthetic X press, jump boost, velocity while native Fall owns Link, late
// Parasail handoff; kept because the pure machine and the host suite reach it.

// Action-thread hook bodies run on the game's action threads and talk to the tick only through DriveMailbox atomics.
#pragma once

#include "HookshotRuntime.hpp"

namespace zonai_hookshot::transport {
// Production carrier tuning.
inline constexpr pure::PositionZipConfig kPositionZipConfig{};
static_assert(kPositionZipConfig.speed == 60.0f);
static_assert(kPositionZipConfig.updateRate == 60.0f);
static_assert(kPositionZipConfig.standoff == 0.5f);

// Laboratory lane only.
constexpr float kDirectionTestArriveRadius = 0.5f;

// Freeze the start pose, line and endpoint; arm Parasail admission. False when the start state
// refuses.
bool startPositionZip(HookshotRuntime& runtime);

// One carrier step, then the endpoint hold once the standoff is reached.
void positionDriveTick(HookshotRuntime& runtime);
void holdPositionEndpoint(HookshotRuntime& runtime);

void onPositionZipEnded(HookshotRuntime& runtime);
void onPositionZipFailed(HookshotRuntime& runtime);

// Stop velocity and release every launch/handoff lane; every non-success exit routes here so a
// boost can never leak onto the next jump.
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

// Runs at the end of the Npad callback: while a launch pulse is live, OR a genuine X press into
// the newest live sample.
void applyLaunchInjection(void* device);

// Jump-boost consumption from the inline hook (SIMD context: no logging here).
bool consumeJumpBoost();

void reportJumpBoostEdge(HookshotRuntime& runtime);

}  // namespace zonai_hookshot::transport
