// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "TransportController.hpp"

#include <nn/util.h>

#include "ActionContext.hpp"
#include "AimRaycaster.hpp"
#include "DirectionTelemetry.hpp"
#include "FacingController.hpp"
#include "HookshotInput.hpp"
#include "HookshotLog.hpp"
#include "HookshotWorld.hpp"

namespace zonai_hookshot::transport {
using namespace zonai_hookshot::pure;

void stopDrive(HookshotRuntime& runtime, const char* reason) {
    auto& drive = runtime.drive;
    drive.forceEntry.store(0, std::memory_order_release);
    drive.admissionWanted.store(0, std::memory_order_release);
    drive.captureActive.store(0, std::memory_order_release);
    clearLaunch(runtime.launchStage.pulse);
    runtime.launchStage.delayLogged = false;
    drive.jumpBoostArmed.store(0, std::memory_order_release);
    if (drive.active.load(std::memory_order_acquire)) {
        drive.active.store(0, std::memory_order_release);
        ZHLOG("DRIVE_STOP reason=%s applied=%u", reason,
              drive.applied.load(std::memory_order_relaxed));
    }
    runtime.positionDrive.path.active = false;
}

void endTrip(HookshotRuntime& runtime, const char* reason) {
    if (runtime.transport.endReason[0]) return;
    nn::util::SNPrintf(runtime.transport.endReason,
                       sizeof(runtime.transport.endReason), "%s", reason);
    stopDrive(runtime, reason);
}

bool startPositionZip(HookshotRuntime& runtime) {
    auto& drive = runtime.drive;
    aim::abandonPending();
    runtime.positionDrive = {};
    runtime.capture = {};
    drive.forceEntry.store(0, std::memory_order_release);
    drive.captureActive.store(0, std::memory_order_release);
    drive.admissionArms.store(0, std::memory_order_relaxed);
    drive.predForced.store(0, std::memory_order_relaxed);
    // Publish before the first forceSetMatrix step so a later native FallEnter can arm the
    // predicate on its own thread.
    drive.admissionWanted.store(1, std::memory_order_release);

    const Vec3 start = world::havePlayer() ? world::playerPosition() : Vec3{};
    runtime.positionDrive.config = kPositionZipConfig;
    const bool rotationOk = facing::captureBasis(runtime);
    const bool pathOk =
        rotationOk && beginPositionZip(runtime.positionDrive.path, start,
                                       runtime.launch.anchor,
                                       runtime.positionDrive.config);
    const Vec3 desiredFacing = wallFacingDirection(
        runtime.aim.committedNormal, runtime.positionDrive.path.direction);
    const bool yawOk =
        pathOk && facing::prepare(runtime, desiredFacing,
                                  runtime.positionDrive.path.travelDistance);
    if (!pathOk || !yawOk) {
        runtime.positionDrive.failed = true;
        drive.admissionWanted.store(0, std::memory_order_release);
        drive.forceEntry.store(0, std::memory_order_release);
        ZHLOG("POSITION_ZIP_START refused rotation=%d path=%d yaw=%d span_dm=%d",
              (int)rotationOk, (int)pathOk, (int)yawOk,
              (int)(distance(start, runtime.launch.anchor) * 10.0f));
        return false;
    }
    ZHLOG("COMPOSE_START span_dm=%d travel_dm=%d step_cm=%d standoff_dm=%d "
          "glide_at_start=%u fall_at_start=%u yaw_mode=%s yaw_mdeg=%d",
          (int)(distance(start, runtime.launch.anchor) * 10.0f),
          (int)(runtime.positionDrive.path.travelDistance * 10.0f),
          (int)(kPositionZipConfig.speed / kPositionZipConfig.updateRate *
                100.0f),
          (int)(kPositionZipConfig.standoff * 10.0f),
          drive.parasailActive.load(std::memory_order_relaxed),
          drive.fallActive.load(std::memory_order_relaxed),
          yawTimingName(runtime.positionDrive.yaw.timing),
          (int)(runtime.positionDrive.yaw.signedRadians * 57295.7795f));
    if (runtime.positionDrive.yaw.timing == YawTiming::Early &&
        !runtime.positionDrive.yaw.finished) {
        ZHLOG("YAW_BEGIN mode=EARLY adv_dm=0 duration_dm=%d angle_mdeg=%d",
              (int)(runtime.positionDrive.yaw.durationDistance * 10.0f),
              (int)(runtime.positionDrive.yaw.signedRadians * 57295.7795f));
    }
    // If A was pressed while already falling, FallEnter has already happened; arm the predicate
    // now.
    if (drive.fallActive.load(std::memory_order_acquire)) {
        drive.admissionArms.fetch_add(1, std::memory_order_relaxed);
        drive.forceEntry.store(1, std::memory_order_release);
        ZHLOG("COMPOSE_GLIDE_ARM source=already_falling");
    }
    return true;
}

void positionDriveTick(HookshotRuntime& runtime) {
    auto& drive = runtime.positionDrive;
    if (!drive.path.active || !world::havePlayer()) return;

    const bool yawStartedNow = facing::startOnParasailIfReady(runtime);

    const Vec3 actual = world::playerPosition();
    if (!finite3(actual)) {
        drive.path.active = false;
        drive.failed = true;
        ZHLOG("POSITION_ZIP_STOP reason=nonfinite_actor_position");
        return;
    }
    drive.shown = actual;
    drive.haveShown = true;
    if (drive.haveRequested) {
        drive.lastAcceptanceError = distance(actual, drive.lastRequested);
        if (drive.lastAcceptanceError > drive.maxAcceptanceError)
            drive.maxAcceptanceError = drive.lastAcceptanceError;
    }

    Vec3 next{};
    const PositionZipResult result =
        stepPositionZip(drive.path, next, drive.config);
    if (result == PositionZipResult::Invalid ||
        result == PositionZipResult::Timeout) {
        drive.failed = true;
        ZHLOG("POSITION_ZIP_STOP reason=%s tick=%d advanced_dm=%d rem_dm=%d",
              result == PositionZipResult::Timeout ? "timeout" : "invalid",
              drive.path.ticks, (int)(drive.path.advanced * 10.0f),
              (int)(positionZipRemaining(drive.path) * 10.0f));
        return;
    }

    // The first glider pose is the exact captured orientation; easing begins on the next update.
    if (!yawStartedNow) facing::advanceOneStep(runtime);
    if (!facing::applyPose(runtime, next)) {
        drive.path.active = false;
        drive.failed = true;
        ZHLOG("POSITION_ZIP_STOP reason=invalid_yaw_basis");
        return;
    }
    drive.lastRequested = next;
    drive.haveRequested = true;
    if (drive.path.ticks == 1 || drive.path.ticks == 10 ||
        drive.path.ticks == 30 || drive.path.ticks % 60 == 0 ||
        result == PositionZipResult::Reached) {
        ZHLOG("POSITION_ZIP_SAMPLE tick=%d advanced_dm=%d rem_dm=%d "
              "accept_err_cm=%d yaw_mode=%s yaw_pct=%d",
              drive.path.ticks, (int)(drive.path.advanced * 10.0f),
              (int)(positionZipRemaining(drive.path) * 10.0f),
              (int)(drive.lastAcceptanceError * 100.0f),
              yawTimingName(drive.yaw.timing),
              (int)(yawLinearProgress(drive.yaw) * 100.0f));
    }
    if (result == PositionZipResult::Reached) drive.completed = true;
}

void holdPositionEndpoint(HookshotRuntime& runtime) {
    auto& drive = runtime.positionDrive;
    if (!drive.completed || !world::havePlayer() ||
        !finite3(drive.path.endpoint)) {
        return;
    }
    const Vec3 actual = world::playerPosition();
    if (finite3(actual)) {
        drive.shown = actual;
        drive.haveShown = true;
    }
    const bool yawStartedNow = facing::startOnParasailIfReady(runtime);
    if (!yawStartedNow) facing::advanceOneStep(runtime);
    if (!facing::applyPose(runtime, drive.path.endpoint)) {
        drive.failed = true;
        ZHLOG("POSITION_ZIP_STOP reason=invalid_held_yaw_basis");
        return;
    }
    drive.lastRequested = drive.path.endpoint;
    drive.haveRequested = true;
}

void onPositionZipEnded(HookshotRuntime& runtime) {
    ZHLOG("POSITION_ZIP_STOP reason=standoff ticks=%d advanced_dm=%d "
          "max_err_cm=%d",
          runtime.positionDrive.path.ticks,
          (int)(runtime.positionDrive.path.advanced * 10.0f),
          (int)(runtime.positionDrive.maxAcceptanceError * 100.0f));
    note("position zip complete: stopped 0.5 m from anchor");
}

void onPositionZipFailed(HookshotRuntime& runtime) {
    stopDrive(runtime, "position/admission failed");
    ZHLOG("POSITION_ZIP_FAILED ticks=%d advanced_dm=%d max_err_cm=%d",
          runtime.positionDrive.path.ticks,
          (int)(runtime.positionDrive.path.advanced * 10.0f),
          (int)(runtime.positionDrive.maxAcceptanceError * 100.0f));
    note("position zip failed its bounded guard");
}

void onDetachRequested(HookshotRuntime& runtime) {
    aim::abandonPending();  // free a stale aim request for cruise probes
    const int tier = runtime.transport.tier;
    runtime.transport = {};
    runtime.transport.tier = tier;
    runtime.transport.requested = true;
    runtime.transport.requestTick = runtime.session.tick;
    runtime.transport.fallUpdatesAtRequest =
        runtime.drive.fallUpdates.load(std::memory_order_relaxed);
    runtime.drive.predForced.store(0, std::memory_order_relaxed);
    runtime.drive.jumpPulses.store(0, std::memory_order_relaxed);
    runtime.drive.jumpBoostHits.store(0, std::memory_order_relaxed);
    runtime.launchStage.lastBoostHits = 0;
    runtime.launchStage.delayLogged = false;
    // Launch plan by observed ownership: ground or climbing arm the synthetic X pulse plus boost;
    // already falling injects nothing; Parasail is an unsupported start.
    const LaunchOrigin origin = classifyOrigin(
        runtime.drive.fallActive.load(std::memory_order_acquire) != 0,
        runtime.drive.parasailActive.load(std::memory_order_acquire) != 0);
    if (beginLaunch(runtime.launchStage.pulse, origin)) {
        // Publish the boost before the injector can mutate X later in this callback.
        runtime.drive.jumpBoostArmed.store(1, std::memory_order_release);
        ZHLOG("ZIP_ARM tick=%d tier=%d origin=solid pulse=%d boost=armed",
              (int)runtime.session.tick, runtime.transport.tier + 1,
              kPulseFrames);
        note("zip: jumping...");
    } else if (origin == LaunchOrigin::Airborne) {
        ZHLOG("ZIP_ARM tick=%d tier=%d origin=air (no injection)",
              (int)runtime.session.tick, runtime.transport.tier + 1);
        note("zip: already falling...");
    } else {
        ZHLOG("LAUNCH_UNSUPPORTED origin=parasail (no injection)");
        note("zip: close the paraglider first");
    }
}

void onDetachRefused(HookshotRuntime& runtime) {
    // pulse=0: no synthetic input applied; pulse>0 boost=0: X arrived but Jump never ran; boost>0
    // fall=0: no native Fall inside the window.
    ZHLOG("LAUNCH_REFUSED pulse=%u boost=%u fall=%u",
          runtime.drive.jumpPulses.load(std::memory_order_relaxed),
          runtime.drive.jumpBoostHits.load(std::memory_order_relaxed),
          runtime.drive.fallUpdates.load(std::memory_order_relaxed) -
              runtime.transport.fallUpdatesAtRequest);
    stopDrive(runtime, "launch refused");
    note("could not launch - still latched (A retries)");
}

void onFallEntered(HookshotRuntime& runtime) {
    // Native Fall owns Link: start the drive and clear the launch one-shots so an unconsumed boost
    // never reaches the next jump.
    clearLaunch(runtime.launchStage.pulse);
    runtime.drive.jumpBoostArmed.store(0, std::memory_order_release);
    const TransportConfig config{};
    const float speed = config.speeds[runtime.transport.tier];
    const Vec3 position =
        world::havePlayer() ? world::playerPosition() : Vec3{};
    const float span = distance(position, runtime.launch.anchor);
    runtime.transport.trip = {};
    runtime.transport.trip.bestRemaining = span;
    runtime.transport.cruiseStartTick = runtime.session.tick;
    runtime.transport.lastFallUpdates =
        runtime.drive.fallUpdates.load(std::memory_order_relaxed);
    runtime.transport.fallLeavesAtCruise =
        runtime.drive.fallLeaves.load(std::memory_order_relaxed);
    runtime.transport.stallTicks = 0;
    // target/speed first, active last.
    auto& drive = runtime.drive;
    drive.target[0] = runtime.launch.anchor.x;
    drive.target[1] = runtime.launch.anchor.y;
    drive.target[2] = runtime.launch.anchor.z;
    drive.speedBits.store(floatToBits(speed), std::memory_order_relaxed);
    drive.remainingBits.store(floatToBits(span), std::memory_order_relaxed);
    drive.observedBits.store(0, std::memory_order_relaxed);
    drive.applied.store(0, std::memory_order_relaxed);
    drive.directionStart[0] = position.x;
    drive.directionStart[1] = position.y;
    drive.directionStart[2] = position.z;
    drive.directionPrevious[0] = position.x;
    drive.directionPrevious[1] = position.y;
    drive.directionPrevious[2] = position.z;
    drive.directionSamples.store(0, std::memory_order_relaxed);
    drive.directionAlignBits.store(floatToBits(0.0f), std::memory_order_relaxed);
    drive.directionSideBits.store(floatToBits(0.0f), std::memory_order_relaxed);
    drive.directionLateralBits.store(floatToBits(0.0f),
                                     std::memory_order_relaxed);
    drive.active.store(1, std::memory_order_release);
    ZHLOG("FALL_ENTER wait=%d pulse=%u boost=%u span_dm=%d tier=%d",
          (int)(runtime.session.tick - runtime.transport.requestTick),
          drive.jumpPulses.load(std::memory_order_relaxed),
          drive.jumpBoostHits.load(std::memory_order_relaxed),
          (int)(span * 10.0f), runtime.transport.tier + 1);
    note("ZIP! flying to the anchor. B bails out");
}

void onTripAborted(HookshotRuntime& runtime) {
    stopDrive(runtime, "aborted");
    ZHLOG("TRIP_ABORT reason=\"%s\" rem_dm=%d ticks=%d applied=%u obs_mps=%d",
          runtime.transport.endReason,
          (int)(bitsToFloat(runtime.drive.remainingBits.load(
                    std::memory_order_relaxed)) *
                10.0f),
          (int)(runtime.session.tick - runtime.transport.cruiseStartTick),
          runtime.drive.applied.load(std::memory_order_relaxed),
          (int)bitsToFloat(
              runtime.drive.observedBits.load(std::memory_order_relaxed)));
    char done[96];
    nn::util::SNPrintf(done, sizeof(done), "zip ended: %s",
                       runtime.transport.endReason);
    note(done);
}

void serviceFallCruise(HookshotRuntime& runtime, MachineInputs& inputs) {
    // The native Fall action must keep answering; the handoff check outranks these in the machine.
    const std::uint32_t fallUpdates =
        runtime.drive.fallUpdates.load(std::memory_order_relaxed);
    if (fallUpdates == runtime.transport.lastFallUpdates) {
        if (++runtime.transport.stallTicks > 10)
            endTrip(runtime, "the fall ended under us");
    } else {
        runtime.transport.stallTicks = 0;
        runtime.transport.lastFallUpdates = fallUpdates;
    }
    if (runtime.drive.fallLeaves.load(std::memory_order_relaxed) !=
        runtime.transport.fallLeavesAtCruise) {
        endTrip(runtime, "the fall ended (landed or caught)");
    }
    // Any hit on the shortened Link-to-anchor segment consumed this tick is an obstacle.
    if (sampleArrived(runtime.aim.sample, runtime.aim.sampleTick,
                      runtime.aim.requestSeq, runtime.session.tick) &&
        runtime.aim.sample.hit && runtime.aim.sample.generation == runtime.session.worldGen) {
        endTrip(runtime, "obstacle ahead - stopped early");
    }
    const float remaining =
        bitsToFloat(runtime.drive.remainingBits.load(std::memory_order_relaxed));
    TransportConfig directionTestConfig{};
    directionTestConfig.arriveRadius = kDirectionTestArriveRadius;
    switch (checkTrip(runtime.transport.trip, remaining, directionTestConfig)) {
        case TripCheck::Arrived: endTrip(runtime, "arrived at the anchor"); break;
        case TripCheck::Timeout: endTrip(runtime, "trip timeout"); break;
        case TripCheck::NoProgress:
            endTrip(runtime, "no progress - something held Link");
            break;
        case TripCheck::BadValues: endTrip(runtime, "bad values guard"); break;
        case TripCheck::Continue: break;
    }
    // Fall-only to physical contact; the forced Parasail handoff belongs to the composition build.
    inputs.handoffReached = false;
    inputs.transportDone = runtime.transport.endReason[0] != 0;
}

void requestCruiseProbe(HookshotRuntime& runtime) {
    if (!world::havePlayer()) return;
    const Vec3 position = world::playerPosition();
    const Vec3 toAnchor = sub(runtime.launch.anchor, position);
    const float remaining = length(toAnchor);
    const TransportConfig config{};
    const float speed = config.speeds[runtime.transport.tier];
    float lookahead = speed * 0.7f + 2.0f;
    const float clearZone = remaining - 2.0f * config.arriveRadius;
    if (lookahead > clearZone) lookahead = clearZone;
    if (!finite3(toAnchor) || remaining <= 1.0f || lookahead <= 1.0f) return;

    const Vec3 to = add(position, mul(toAnchor, lookahead / remaining));
    Vec3 direction = sub(to, position);
    const float directionLength = length(direction);
    if (!finite3(direction) || directionLength < 0.5f) return;
    direction = mul(direction, 1.0f / directionLength);
    // Segment origin captured by value.
    (void)aim::request(position, to, position, direction,
                       runtime.aim.requestSeq, runtime.session.worldGen,
                       runtime.session.tick);
}

void reportJumpBoostEdge(HookshotRuntime& runtime) {
    const std::uint32_t hits =
        runtime.drive.jumpBoostHits.load(std::memory_order_relaxed);
    if (hits == runtime.launchStage.lastBoostHits) return;
    runtime.launchStage.lastBoostHits = hits;
    ZHLOG("JUMP_BOOST_CONSUMED hits=%u", hits);
}

void onFallEnterHook(void*) {
    HookshotRuntime& rt = runtime();
    rt.drive.fallEnters.fetch_add(1, std::memory_order_relaxed);
    rt.drive.fallActive.store(1, std::memory_order_release);
    if (rt.drive.admissionWanted.load(std::memory_order_acquire)) {
        const std::uint32_t arms =
            rt.drive.admissionArms.fetch_add(1, std::memory_order_relaxed) + 1;
        rt.drive.forceEntry.store(1, std::memory_order_release);
        ZHLOG("COMPOSE_GLIDE_ARM source=fall_enter arms=%u", arms);
    }
}

void onFallLeaveHook(void*) {
    HookshotRuntime& rt = runtime();
    rt.drive.fallLeaves.fetch_add(1, std::memory_order_relaxed);
    rt.drive.fallActive.store(0, std::memory_order_release);
    // Fail-closed: never keep driving once the hosting action is gone.
    rt.drive.active.store(0, std::memory_order_release);
}

void onFallUpdateHook(void* actionObject) {
    HookshotRuntime& rt = runtime();
    auto& drive = rt.drive;
    drive.fallUpdates.fetch_add(1, std::memory_order_relaxed);
    drive.fallActive.store(1, std::memory_order_release);
    if (!drive.active.load(std::memory_order_acquire) || !actionObject) return;

    const action::Context context = action::resolve(actionObject);
    if (!context.ok()) {
        drive.active.store(0, std::memory_order_release);
        ZHLOG("DRIVE_BAIL unresolved actor=%d player=%d",
              (int)context.actorOk(), (int)context.playerOk());
        return;
    }
    const Vec3 position = action::actorPosition(context);
    const Vec3 target{drive.target[0], drive.target[1], drive.target[2]};
    if (!finite3(position) || !finite3(target)) {
        drive.active.store(0, std::memory_order_release);
        ZHLOG("DRIVE_BAIL non-finite position/target");
        return;
    }
    // Velocity before this tick's set is the last physics step's outcome.
    const Vec3 observed = action::actorVelocity(context);
    if (finite3(observed))
        drive.observedBits.store(floatToBits(length(observed)),
                                 std::memory_order_relaxed);
    drive.remainingBits.store(floatToBits(distance(position, target)),
                              std::memory_order_relaxed);
    const float speed =
        bitsToFloat(drive.speedBits.load(std::memory_order_relaxed));
    const Vec3 desired = cruiseVelocity(position, target, speed);
    if (!finite3(desired)) return;
    const Vec3 start{drive.directionStart[0], drive.directionStart[1],
                     drive.directionStart[2]};
    const Vec3 previous{drive.directionPrevious[0], drive.directionPrevious[1],
                        drive.directionPrevious[2]};
    const DirectionTelemetry direction =
        measureDirection(start, target, previous, position);
    if (direction.valid) {
        drive.directionPrevious[0] = position.x;
        drive.directionPrevious[1] = position.y;
        drive.directionPrevious[2] = position.z;
        drive.directionAlignBits.store(floatToBits(direction.stepAlignment),
                                       std::memory_order_relaxed);
        drive.directionSideBits.store(
            floatToBits(direction.signedHorizontalSine),
            std::memory_order_relaxed);
        drive.directionLateralBits.store(floatToBits(direction.lineLateral),
                                         std::memory_order_relaxed);
        const std::uint32_t sample =
            drive.directionSamples.fetch_add(1, std::memory_order_relaxed) + 1;
        if (sample == 1 || sample == 10 || sample == 30 || sample == 60 ||
            sample % 120 == 0) {
            ZHLOG(
                "DIRECTION_SAMPLE n=%u align_milli=%d side_milli=%d "
                "lateral_cm=%d step_mm=%d rem_dm=%d req_dms=(%d,%d,%d) "
                "obs_dms=(%d,%d,%d)",
                sample, (int)(direction.stepAlignment * 1000.0f),
                (int)(direction.signedHorizontalSine * 1000.0f),
                (int)(direction.lineLateral * 100.0f),
                (int)(direction.stepDistance * 1000.0f),
                (int)(distance(position, target) * 10.0f),
                (int)(desired.x * 10.0f), (int)(desired.y * 10.0f),
                (int)(desired.z * 10.0f), (int)(observed.x * 10.0f),
                (int)(observed.y * 10.0f), (int)(observed.z * 10.0f));
        }
    }
    // After the vanilla Fall update so its solver cannot overwrite this.
    action::setLinearVelocity(context, desired);
    drive.applied.fetch_add(1, std::memory_order_relaxed);
}

void applyLaunchInjection(void* device) {
    HookshotRuntime& rt = runtime();
    if (!device) return;

    if (rt.launchStage.pulse.pulseFramesLeft <= 0) return;
    const input::LiveSlot slot = input::newestLiveSlot(device);
    if (!slot.valid()) return;
    const bool fresh = slot.samplingNumber != rt.launchStage.lastInjectSampling;
    const bool physicalX = input::slotHolds(slot, input::kButtonX);
    if (fresh) rt.launchStage.lastInjectSampling = slot.samplingNumber;
    if (!pulseMutates(rt.launchStage.pulse, fresh, physicalX)) {
        if (fresh && physicalX && !rt.launchStage.delayLogged) {
            rt.launchStage.delayLogged = true;
            ZHLOG("JUMP_PULSE delayed physical_x=1 slot=%d (waiting for release)",
                  slot.index);
        }
        return;
    }
    input::pressInSlot(slot, input::kButtonX);
    rt.drive.jumpPulses.fetch_add(1, std::memory_order_relaxed);
    ZHLOG("JUMP_PULSE left=%d slot=%d physical_x=0",
          rt.launchStage.pulse.pulseFramesLeft, slot.index);
}

bool consumeJumpBoost() {
    HookshotRuntime& rt = runtime();
    if (rt.drive.jumpBoostArmed.exchange(0, std::memory_order_acq_rel) == 0)
        return false;
    rt.drive.jumpBoostHits.fetch_add(1, std::memory_order_relaxed);
    return true;
}

}  // namespace zonai_hookshot::transport
