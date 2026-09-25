// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "TargetingService.hpp"

#include "AimRaycaster.hpp"
#include "ChainPresentation.hpp"
#include "HookshotLog.hpp"
#include "HookshotWorld.hpp"

namespace zonai_hookshot::targeting {
using namespace zonai_hookshot::pure;

Vec3 chainOrigin() {
    Vec3 origin = world::playerPosition();
    origin.y += kOriginUp;
    return origin;
}

void serviceAimRay(HookshotRuntime& runtime) {
    TargetSample sample{};
    const aim::Poll poll = aim::service(runtime.session.tick, sample);
    if (poll == aim::Poll::TimedOut) {
        ++runtime.aim.starved;
        ZHLOG("RAY_TIMEOUT seq=%u starved=%d", runtime.aim.requestSeq,
              runtime.aim.starved);
        return;
    }
    if (poll != aim::Poll::Ready) return;
    if (sample.hit && world::havePlayer())
        sample.range = distance(sample.position, chainOrigin());
    runtime.aim.sample = sample;
    runtime.aim.sampleTick = runtime.session.tick;
    runtime.aim.starved = 0;
}

void requestAimRay(HookshotRuntime& runtime) {
    if (!world::havePlayer() || world::state().camera == 0) return;
    float rotation[9]{};
    if (!world::readCameraRotation(rotation)) return;
    Vec3 direction{-rotation[2], -rotation[5], -rotation[8]};
    const float directionLength = length(direction);
    if (!finite3(direction) || directionLength < 0.5f) return;
    direction = mul(direction, 1.0f / directionLength);
    // Lean the aim ray right and up so the reticle clears Link; a bad basis vector is skipped
    // rather than poisoning the ray.
    const Vec3 right{rotation[0], rotation[3], rotation[6]};
    const Vec3 up{rotation[1], rotation[4], rotation[7]};
    const float rightLength = length(right);
    const float upLength = length(up);
    Vec3 leaned = direction;
    if (finite3(right) && rightLength > 0.5f) {
        leaned = add(leaned, mul(right, kAimRightOffset / rightLength));
    }
    if (finite3(up) && upLength > 0.5f)
        leaned = add(leaned, mul(up, kAimUpOffset / upLength));
    const float leanedLength = length(leaned);
    if (finite3(leaned) && leanedLength > 0.5f)
        direction = mul(leaned, 1.0f / leanedLength);
    const Vec3 from = world::cameraPosition();
    const Vec3 to = add(from, mul(direction, kCastLength));
    // The player position at request time, by value: the worker never dereferences the player
    // pointer.
    const Vec3 nearPoint = world::playerPosition();
    (void)aim::request(from, to, nearPoint, direction, runtime.aim.requestSeq,
                       runtime.session.worldGen, runtime.session.tick);
}

const char* refusalText(Verdict verdict) {
    switch (verdict) {
        case Verdict::NoClimb:             return "REFUSED: that surface refuses climbing";
        case Verdict::Floor:               return "REFUSED: that is a floor";
        case Verdict::CeilingOrOverhang:   return "REFUSED: ceiling/strong overhang";
        case Verdict::TooNear:             return "REFUSED: too close";
        case Verdict::TooFar:              return "REFUSED: out of range";
        case Verdict::Backface:            return "REFUSED: facing away";
        case Verdict::MovingOrUnsupported: return "REFUSED: not solid terrain";
        case Verdict::Miss:                return "REFUSED: nothing there";
        default:                           return "REFUSED: no fresh target";
    }
}

void onTargetingEntered(HookshotRuntime&) {
    note("aim with camera; A: HOOK, B: cancel");
    ZHLOG("TARGETING_ENTER");
}

void onArmingAbandoned(HookshotRuntime&) {
    note("hold ZL + L a moment longer to aim");
}

void onConfirmStarted(HookshotRuntime& runtime) {
    runtime.aim.confirmFloorSeq = runtime.aim.requestSeq + 1;
    // Free an unclaimed pre-press request so the post-press ray goes out this tick.
    aim::abandonPending();
    note("locking target...");
    ZHLOG("CONFIRM_START floor_seq=%u", runtime.aim.confirmFloorSeq);
}

void onCommitted(HookshotRuntime& runtime) {
    runtime.aim.latchFeedbackTicks = 0;
    runtime.launch = {};
    runtime.launch.fireOrigin = chainOrigin();
    runtime.launch.anchor = runtime.aim.sample.position;  // exact ray hit
    runtime.aim.committedNormal = runtime.aim.sample.normal;
    runtime.aim.committedFlags = runtime.aim.sample.shapeFlags;
    runtime.aim.committedRange = runtime.aim.sample.range;
    note("HOOK! chain firing");
    ZHLOG("COMMIT seq=%u range_cm=%d ny_x100=%d mt=%u flags=0x%x",
          runtime.aim.sample.sequence,
          (int)(runtime.aim.sample.range * 100.0f),
          (int)(runtime.aim.sample.normal.y * 100.0f),
          runtime.aim.sample.hitMotionType, runtime.aim.committedFlags);
}

void onRefused(HookshotRuntime& runtime) {
    const Verdict reason = confirmRefusalReason(runtime.aim.sample,
        sampleContext(runtime.aim.sample, runtime.aim.sampleTick,
                      runtime.session.worldGen, runtime.session.tick),
                                                runtime.aim.confirmFloorSeq);
    note(refusalText(reason));
    ZHLOG("TARGET_REFUSED reason=%s", verdictName(reason));
}

void onLatched(HookshotRuntime& runtime) {
    runtime.aim.latchFeedbackTicks = kLatchFeedbackTicks;
    note("latched - zipping automatically; B clears");
    ZHLOG("CHAIN_LATCHED ticks=%d range_cm=%d auto_zip=1", runtime.launch.ticks,
          (int)(runtime.aim.committedRange * 100.0f));
}

}  // namespace zonai_hookshot::targeting
