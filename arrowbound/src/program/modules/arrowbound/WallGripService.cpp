// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "WallGripService.hpp"

#include "ActionContext.hpp"
#include "AimRaycaster.hpp"
#include "HookshotInput.hpp"
#include "HookshotLog.hpp"
#include "HookshotWorld.hpp"
#include "../../../pure/FlightDiagnostics.hpp"

namespace arrowbound::wall_grip {
using namespace pure;
namespace {
constexpr WallGripConfig kWall{};
constexpr CaptureConfig kCapture{};
}

void stop(HookshotRuntime& rt) {
    rt.drive.captureActive.store(0, std::memory_order_release);
    if (rt.grip.armed.exchange(0, std::memory_order_acq_rel)) aim::abandonPending();
}

bool begin(HookshotRuntime& rt, Vec3 contact, Vec3 direction) {
    Vec3 from{}, to{};
    if (!rt.grip.hooksReady.load(std::memory_order_acquire) ||
        !wallProbeSegment(contact, direction, from, to, kWall)) {
        ZHLOG("WALL_GRIP_SKIP reason=hooks_or_impact hooks=%u",
              rt.grip.hooksReady.load(std::memory_order_relaxed));
        return false;
    }
    auto& wall = rt.arrowTrip.wall;
    wall = {};
    wall.impact = contact;
    wall.direction = direction;
    wall.watch.startedTick = rt.session.tick;
    wall.watch.climbBaseline = rt.grip.climbUpdates.load(std::memory_order_acquire);
    wall.watch.lastUpdates = rt.drive.updates.load(std::memory_order_acquire);
    rt.grip.rejected.store(0, std::memory_order_relaxed);
    rt.grip.applied.store(0, std::memory_order_relaxed);
    rt.grip.inputApplied.store(0, std::memory_order_relaxed);
    rt.grip.armed.store(1, std::memory_order_release);
    rt.arrowTrip.phase = ArrowPhase::WallProbe;
    ZHLOG("WALL_PROBE_BEGIN shot=%u", rt.arrowTrip.shotSeqSeen);
    return true;
}

const char* service(HookshotRuntime& rt) {
    auto& wall = rt.arrowTrip.wall;
    auto& grip = rt.grip;
    if (grip.climbUpdates.load(std::memory_order_acquire) != wall.watch.climbBaseline)
        return "climb";
    if (rt.arrowTrip.phase == ArrowPhase::WallProbe) {
        if (arrowTimedOut(rt.session.tick, wall.watch.startedTick, kWall.probeTimeoutTicks))
            return "wall confirmation timeout";
        if (!wall.request) {
            Vec3 from{}, to{};
            if (!wallProbeSegment(wall.impact, wall.direction, from, to, kWall))
                return "invalid wall probe";
            if (aim::request(from, to, world::playerPosition(), wall.direction, rt.aim.requestSeq,
                             rt.session.worldGen, rt.session.tick))
                wall.request = rt.aim.requestSeq;
        }
        if (!wall.confirmed && sampleArrived(rt.aim.sample, rt.aim.sampleTick,
                                             wall.request, rt.session.tick)) {
            const auto verdict = wallGripSurface(rt.aim.sample, wall.impact, world::playerPosition(),
                rt.session.worldGen, wall.request, kWall);
#if ARROWBOUND_FLIGHT_DIAGNOSTICS
            ZHLOG("TRACE_WALL shot=%u req=%u seq=%u gen=%u expected_gen=%u hit=%u known=%u motion=%u flags=%08x contact_error_cm=%d player_distance_cm=%d outside_cm=%d",
                  rt.arrowTrip.shotSeqSeen, wall.request, rt.aim.sample.sequence,
                  rt.aim.sample.generation, rt.session.worldGen, (unsigned)rt.aim.sample.hit,
                  (unsigned)rt.aim.sample.hitBodyKnown, rt.aim.sample.hitMotionType,
                  rt.aim.sample.shapeFlags, traceNumber(distance(rt.aim.sample.position, wall.impact)),
                  traceNumber(distance(world::playerPosition(), rt.aim.sample.position)),
                  traceNumber(dot(sub(world::playerPosition(), rt.aim.sample.position), rt.aim.sample.normal)));
#endif
            ZHLOG("WALL_PROBE_RESULT shot=%u verdict=%s normal_milli=(%d,%d,%d)",
                  rt.arrowTrip.shotSeqSeen, verdictName(verdict),
                  finite3(rt.aim.sample.normal) ? (int)(rt.aim.sample.normal.x * 1000) : 0,
                  finite3(rt.aim.sample.normal) ? (int)(rt.aim.sample.normal.y * 1000) : 0,
                  finite3(rt.aim.sample.normal) ? (int)(rt.aim.sample.normal.z * 1000) : 0);
            if (verdict != Verdict::Valid) return "wall surface refused";
            wall.target = rt.aim.sample.position;
            wall.normal = mul(rt.aim.sample.normal, 1.0f / length(rt.aim.sample.normal));
            wall.confirmed = true;
        }
        if (!wall.confirmed || !rt.drive.parasailActive.load(std::memory_order_acquire)) return nullptr;
        if (!grip.npadValid.load(std::memory_order_acquire)) return "wall controller unavailable";
        const Vec3 position = world::playerPosition();
        if (!finite3(position) || distance(position, wall.target) > kWall.maxApproachDistance ||
            dot(sub(position, wall.target), wall.normal) < kWall.minOutsideDistance) {
#if ARROWBOUND_FLIGHT_DIAGNOSTICS
            ZHLOG("TRACE_WALL_ENVELOPE shot=%u distance_cm=%d outside_cm=%d glider=%u",
                  rt.arrowTrip.shotSeqSeen, traceNumber(distance(position, wall.target)),
                  traceNumber(dot(sub(position, wall.target), wall.normal)), rt.drive.parasailActive.load());
#endif
            return "wall approach outside safe envelope";
        }
        float basis[9]{}, facing[9]{};
        if (!world::readPlayerRotation(basis) ||
            !wallGripBasis(basis, wall.normal, wall.direction, facing) ||
            !world::forcePlayerPose(facing, position))
            return "wall facing unavailable";
        grip.target.store(wall.target);
        grip.normal.store(wall.normal);
        wall.watch.startedTick = rt.session.tick;
        wall.watch.lastUpdates = rt.drive.updates.load(std::memory_order_acquire);
        rt.drive.admissionWanted.store(0, std::memory_order_release);
        rt.drive.forceEntry.store(0, std::memory_order_release);
        rt.arrowTrip.phase = ArrowPhase::WallGrip;
        rt.drive.captureActive.store(1, std::memory_order_release);
        ZHLOG("WALL_GRIP_BEGIN shot=%u distance_cm=%d npad=%u", rt.arrowTrip.shotSeqSeen,
              (int)(distance(position, wall.target) * 100), grip.npadId.load(std::memory_order_relaxed));
        return nullptr;
    }
    const auto progress = wall.watch.update(rt.session.tick,
        grip.climbUpdates.load(std::memory_order_acquire),
        grip.rejected.load(std::memory_order_acquire) != 0,
        rt.drive.parasailActive.load(std::memory_order_acquire) != 0,
        rt.drive.updates.load(std::memory_order_acquire), kCapture);
    switch (progress) {
        case GripProgress::Waiting: return nullptr;
        case GripProgress::Acquired: return "climb";
        case GripProgress::Rejected: return "wall motion rejected";
        case GripProgress::Stalled: return "wall glider updates stalled";
        case GripProgress::LeftGlider: return "wall glider left without climb";
        case GripProgress::TimedOut: return "wall grip timeout";
    }
    return "wall grip unknown state";
}

void onParasailUpdate(void* object) {
    auto& rt = runtime();
    if (!rt.drive.captureActive.load(std::memory_order_acquire)) return;
    const auto context = action::resolve(object);
    const Vec3 target = rt.grip.target.load(), normal = rt.grip.normal.load();
    const Vec3 position = context.ok() ? action::actorPosition(context) : Vec3{};
    const Vec3 velocity = captureVelocity(position, target, normal, kCapture);
    if (!context.ok() || context.actor != rt.arrow.playerActor.load(std::memory_order_acquire) ||
        !finite3(position) || distance(position, target) > kWall.maxApproachDistance ||
        dot(sub(position, target), normal) < 0 || length(velocity) < 0.1f) {
        rt.drive.captureActive.store(0, std::memory_order_release);
        rt.grip.rejected.store(1, std::memory_order_release);
        ZHLOG("WALL_GRIP_REJECT actor_ok=%u player_ok=%u distance_cm=%d",
              (unsigned)context.actorOk(), (unsigned)context.playerOk(),
              finite3(position) && finite3(target) ? (int)(distance(position, target) * 100) : -1);
        return;
    }
    action::setLinearVelocity(context, velocity);
    const auto applied = rt.grip.applied.fetch_add(1, std::memory_order_relaxed) + 1;
    if (applied == 1 || applied == 10 || applied == 30)
        ZHLOG("WALL_GRIP_SAMPLE n=%u distance_cm=%d input=%u", applied,
              (int)(distance(position, target) * 100),
              rt.grip.inputApplied.load(std::memory_order_relaxed));
}

void onClimbUpdate(void*) {
    auto& rt = runtime();
    if (rt.grip.armed.exchange(0, std::memory_order_acq_rel)) {
        rt.drive.captureActive.store(0, std::memory_order_release);
        rt.drive.presentParaglider.store(0, std::memory_order_release);
        rt.drive.admissionWanted.store(0, std::memory_order_release);
        rt.drive.forceEntry.store(0, std::memory_order_release);
        ZHLOG("WALL_GRIP_CLIMB applied=%u input=%u",
              rt.grip.applied.load(std::memory_order_relaxed),
              rt.grip.inputApplied.load(std::memory_order_relaxed));
    }
    rt.grip.climbUpdates.fetch_add(1, std::memory_order_release);
}

void onControllerUpdate(void* controller) {
    auto& rt = runtime();
    if (!rt.drive.captureActive.load(std::memory_order_acquire)) return;
    input::ProcessedController processed{};
    if (!input::openProcessedController(controller, processed) ||
        !wallGripOwnsInput(rt.drive.captureActive.load(std::memory_order_acquire) != 0,
            rt.grip.npadValid.load(std::memory_order_acquire) != 0,
            rt.grip.npadId.load(std::memory_order_relaxed), processed.npadId, processed.samplingNumber))
        return;
    processed.leftStick[0] = 0;
    processed.leftStick[1] = 1;
    rt.grip.inputApplied.fetch_add(1, std::memory_order_relaxed);
}
} // namespace arrowbound::wall_grip
