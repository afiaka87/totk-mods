// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// The last metre: a bounded physical approach while native Parasail owns Link, with full-forward intent through the
// processed-stick seam; an observed ExecutePlayerClimb update is the success signal.

// The wall-contact bit is secondary telemetry only.
#pragma once

#include "ActionContext.hpp"
#include "HookshotRuntime.hpp"

namespace zonai_hookshot::capture {
// Terminal-approach tuning.
inline constexpr pure::CaptureConfig kConfig{};
static_assert(kConfig.speed == 4.0f);

void onCaptureStarted(HookshotRuntime& runtime);
void onClimbAcquired(HookshotRuntime& runtime);
void onCaptureFailed(HookshotRuntime& runtime);

// First reason wins; a non-empty reason is the machine's captureFailed signal.
void endCapture(HookshotRuntime& runtime, const char* reason);

void serviceCapture(HookshotRuntime& runtime, pure::MachineInputs& inputs);

void serviceInputTelemetry(HookshotRuntime& runtime);

// Terminal velocity applied from the native Parasail update while capture owns Link.
void applyTerminalVelocity(void* action);

// Native Climb ownership heartbeat.
void onClimbUpdateHook(void* action);

// After NinJoyNpadController::calcImpl_ and before Controller::calc consumes mLeftStick;
// captureActive is also the release signal.
void applyContinuousForward(void* controller);

}  // namespace zonai_hookshot::capture
