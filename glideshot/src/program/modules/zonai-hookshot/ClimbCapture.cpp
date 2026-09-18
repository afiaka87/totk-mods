// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "ClimbCapture.hpp"

#include <nn/util.h>

#include "FacingController.hpp"
#include "HookshotInput.hpp"
#include "HookshotLog.hpp"
#include "HookshotWorld.hpp"
#include "TransportController.hpp"

namespace zonai_hookshot::capture {
using namespace zonai_hookshot::pure;

void endCapture(HookshotRuntime& runtime, const char* reason) {
    if (runtime.capture.endReason[0]) return;
    nn::util::SNPrintf(runtime.capture.endReason,
                       sizeof(runtime.capture.endReason), "%s", reason);
    runtime.drive.captureActive.store(0, std::memory_order_release);
}

void onCaptureStarted(HookshotRuntime& runtime) {
    auto& drive = runtime.drive;
    drive.admissionWanted.store(0, std::memory_order_release);
    drive.forceEntry.store(0, std::memory_order_release);
    runtime.capture = {};
    runtime.capture.startedTick = runtime.session.tick;
    runtime.capture.climbUpdatesAtStart =
        drive.climbUpdates.load(std::memory_order_relaxed);
    runtime.capture.parasailLeavesAtStart =
        drive.leaves.load(std::memory_order_relaxed);
    runtime.capture.lastParasailUpdates =
        drive.updates.load(std::memory_order_relaxed);
    const Vec3 position =
        world::havePlayer() ? world::playerPosition() : Vec3{};
    runtime.capture.startDistance = distance(position, runtime.launch.anchor);
    runtime.capture.facingErrorMilliDegrees =
        facing::facingErrorMilliDegrees(runtime);
    drive.target[0] = runtime.launch.anchor.x;
    drive.target[1] = runtime.launch.anchor.y;
    drive.target[2] = runtime.launch.anchor.z;
    drive.captureNormal[0] = runtime.aim.committedNormal.x;
    drive.captureNormal[1] = runtime.aim.committedNormal.y;
    drive.captureNormal[2] = runtime.aim.committedNormal.z;
    drive.speedBits.store(floatToBits(kConfig.speed), std::memory_order_relaxed);
    drive.remainingBits.store(floatToBits(runtime.capture.startDistance),
                              std::memory_order_relaxed);
    drive.captureApplied.store(0, std::memory_order_relaxed);
    drive.captureBails.store(0, std::memory_order_relaxed);
    // Reset the stick writer's telemetry before publishing capture ownership.
    auto& walk = runtime.walk;
    walk.applications.store(0, std::memory_order_relaxed);
    walk.otherControllerCalls.store(0, std::memory_order_relaxed);
    walk.repeatedSamplingNumber.store(0, std::memory_order_relaxed);
    walk.lastSamplingNumber.store(0, std::memory_order_relaxed);
    walk.beforeXBits.store(floatToBits(0.0f), std::memory_order_relaxed);
    walk.beforeYBits.store(floatToBits(0.0f), std::memory_order_relaxed);
    // All mailbox values are visible before the Parasail hook may act.
    drive.captureActive.store(1, std::memory_order_release);
    ZHLOG("CAPTURE_START rem_dm=%d speed_dms=%d arms=%u forced=%u "
          "wallbit=%d input_npad=%u input_valid=%u yaw_mode=%s yaw_pct=%d "
          "facing_err_mdeg=%d",
          (int)(runtime.capture.startDistance * 10.0f),
          (int)(kConfig.speed * 10.0f),
          drive.admissionArms.load(std::memory_order_relaxed),
          drive.predForced.load(std::memory_order_relaxed),
          (int)world::climbBitEngaged(),
          walk.targetNpadId.load(std::memory_order_relaxed),
          walk.targetNpadValid.load(std::memory_order_relaxed),
          yawTimingName(runtime.positionDrive.yaw.timing),
          (int)(yawLinearProgress(runtime.positionDrive.yaw) * 100.0f),
          runtime.capture.facingErrorMilliDegrees);
    note("physical wall capture + forward intent - native Climb decides");
}

void onClimbAcquired(HookshotRuntime& runtime) {
    transport::stopDrive(runtime, "native Climb acquired");
    ZHLOG("CAPTURE_SUCCESS ticks=%d applied=%u input=%u rem_dm=%d wallbit=%d "
          "facing_err_mdeg=%d",
          (int)(runtime.session.tick - runtime.capture.startedTick),
          runtime.drive.captureApplied.load(std::memory_order_relaxed),
          runtime.walk.applications.load(std::memory_order_relaxed),
          (int)(bitsToFloat(runtime.drive.remainingBits.load(
                    std::memory_order_relaxed)) *
                10.0f),
          (int)world::climbBitEngaged(),
          runtime.capture.facingErrorMilliDegrees);
    note("CLIMB! controls released to the game");
}

void onCaptureFailed(HookshotRuntime& runtime) {
    transport::stopDrive(runtime, "capture failed");
    ZHLOG("CAPTURE_FAILED reason=\"%s\" ticks=%d applied=%u input=%u "
          "input_other=%u rem_dm=%d climb=%u wallbit=%d facing_err_mdeg=%d",
          runtime.capture.endReason,
          (int)(runtime.session.tick - runtime.capture.startedTick),
          runtime.drive.captureApplied.load(std::memory_order_relaxed),
          runtime.walk.applications.load(std::memory_order_relaxed),
          runtime.walk.otherControllerCalls.load(std::memory_order_relaxed),
          (int)(bitsToFloat(runtime.drive.remainingBits.load(
                    std::memory_order_relaxed)) *
                10.0f),
          runtime.drive.climbUpdates.load(std::memory_order_relaxed) -
              runtime.capture.climbUpdatesAtStart,
          (int)world::climbBitEngaged(),
          runtime.capture.facingErrorMilliDegrees);
    note("NO GRAB - physical capture ended safely");
}

void serviceCapture(HookshotRuntime& runtime, MachineInputs& inputs) {
    inputs.climbEntered =
        runtime.drive.climbUpdates.load(std::memory_order_acquire) !=
        runtime.capture.climbUpdatesAtStart;
    if (!inputs.climbEntered) {
        if (runtime.drive.captureBails.load(std::memory_order_relaxed) != 0)
            endCapture(runtime, "capture hook rejected values");
        const std::uint32_t updates =
            runtime.drive.updates.load(std::memory_order_relaxed);
        const bool parasailActive =
            runtime.drive.parasailActive.load(std::memory_order_acquire) != 0;
        if (parasailActive) {
            runtime.capture.leaveGraceTicks = 0;
            if (updates == runtime.capture.lastParasailUpdates) {
                if (++runtime.capture.stalledTicks > 10)
                    endCapture(runtime, "Parasail updates stalled");
            } else {
                runtime.capture.stalledTicks = 0;
                runtime.capture.lastParasailUpdates = updates;
            }
        } else if (++runtime.capture.leaveGraceTicks > kConfig.leaveGraceTicks) {
            endCapture(runtime, "Parasail left without Climb");
        }
        if ((int)(runtime.session.tick - runtime.capture.startedTick) >=
            kConfig.timeoutTicks) {
            endCapture(runtime, "capture timeout");
        }
    }
    inputs.captureFailed = runtime.capture.endReason[0] != 0;
}

void serviceInputTelemetry(HookshotRuntime& runtime) {
    if (runtime.machine.phase != Phase::Capture) return;
    const std::uint32_t applied =
        runtime.walk.applications.load(std::memory_order_acquire);
    constexpr std::uint32_t milestones[] = {1, 10, 30};
    for (std::uint32_t milestone : milestones) {
        if (runtime.capture.inputLastReportedApplications < milestone &&
            applied >= milestone) {
            ZHLOG(
                "CAPTURE_INPUT_SAMPLE n=%u npad=%u valid=%u pre_milli=(%d,%d) "
                "rem_dm=%d",
                milestone,
                runtime.walk.targetNpadId.load(std::memory_order_relaxed),
                runtime.walk.targetNpadValid.load(std::memory_order_relaxed),
                (int)(bitsToFloat(runtime.walk.beforeXBits.load(
                          std::memory_order_relaxed)) *
                      1000.0f),
                (int)(bitsToFloat(runtime.walk.beforeYBits.load(
                          std::memory_order_relaxed)) *
                      1000.0f),
                (int)(bitsToFloat(runtime.drive.remainingBits.load(
                          std::memory_order_relaxed)) *
                      10.0f));
        }
    }
    if (applied > runtime.capture.inputLastReportedApplications)
        runtime.capture.inputLastReportedApplications = applied;
}

void applyTerminalVelocity(void* actionObject) {
    HookshotRuntime& rt = runtime();
    auto& drive = rt.drive;
    if (!drive.captureActive.load(std::memory_order_acquire) || !actionObject)
        return;

    const action::Context context = action::resolve(actionObject);
    if (!context.ok()) {
        drive.captureActive.store(0, std::memory_order_release);
        drive.captureBails.fetch_add(1, std::memory_order_relaxed);
        ZHLOG("CAPTURE_BAIL unresolved actor=%d player=%d",
              (int)context.actorOk(), (int)context.playerOk());
        return;
    }
    const Vec3 position = action::actorPosition(context);
    const Vec3 target{drive.target[0], drive.target[1], drive.target[2]};
    const Vec3 normal{drive.captureNormal[0], drive.captureNormal[1],
                      drive.captureNormal[2]};
    const Vec3 desired = captureVelocity(position, target, normal, kConfig);
    if (!finite3(position) || !finite3(target) || length(desired) < 0.1f) {
        drive.captureActive.store(0, std::memory_order_release);
        drive.captureBails.fetch_add(1, std::memory_order_relaxed);
        ZHLOG("CAPTURE_BAIL invalid position/target/vector");
        return;
    }
    const float remaining = distance(position, target);
    drive.remainingBits.store(floatToBits(remaining), std::memory_order_relaxed);
    action::setLinearVelocity(context, desired);
    const std::uint32_t applied =
        drive.captureApplied.fetch_add(1, std::memory_order_relaxed) + 1;
    if (applied == 1 || applied == 10 || applied == 30 || applied % 60 == 0) {
        ZHLOG("CAPTURE_SAMPLE n=%u rem_dm=%d vel_dms=(%d,%d,%d)", applied,
              (int)(remaining * 10.0f), (int)(desired.x * 10.0f),
              (int)(desired.y * 10.0f), (int)(desired.z * 10.0f));
    }
}

void onClimbUpdateHook(void*) {
    HookshotRuntime& rt = runtime();
    const std::uint32_t updates =
        rt.drive.climbUpdates.fetch_add(1, std::memory_order_relaxed) + 1;
    if (rt.drive.captureActive.exchange(0, std::memory_order_acq_rel) != 0) {
        ZHLOG("CAPTURE_CLIMB_ACTION update=%u applied=%u input=%u", updates,
              rt.drive.captureApplied.load(std::memory_order_relaxed),
              rt.walk.applications.load(std::memory_order_relaxed));
    }
}

void applyContinuousForward(void* controller) {
    HookshotRuntime& rt = runtime();
    if (!controller) return;
    if (rt.drive.captureActive.load(std::memory_order_acquire) == 0 ||
        rt.walk.targetNpadValid.load(std::memory_order_acquire) == 0) {
        return;
    }

    input::ProcessedController processed{};
    if (!input::openProcessedController(controller, processed)) return;
    if (processed.npadId != rt.walk.targetNpadId.load(std::memory_order_relaxed)) {
        rt.walk.otherControllerCalls.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (processed.samplingNumber == 0) return;

    rt.walk.beforeXBits.store(floatToBits(processed.leftStick[0]),
                              std::memory_order_relaxed);
    rt.walk.beforeYBits.store(floatToBits(processed.leftStick[1]),
                              std::memory_order_relaxed);
    const std::uint64_t previous = rt.walk.lastSamplingNumber.exchange(
        processed.samplingNumber, std::memory_order_relaxed);
    if (previous == processed.samplingNumber)
        rt.walk.repeatedSamplingNumber.fetch_add(1, std::memory_order_relaxed);

    // After native normalization, before Controller::calc: continuous full-forward intent in left-
    // stick coordinates.
    processed.leftStick[0] = 0.0f;
    processed.leftStick[1] = 1.0f;

    rt.walk.applications.fetch_add(1, std::memory_order_acq_rel);
}

}  // namespace zonai_hookshot::capture
