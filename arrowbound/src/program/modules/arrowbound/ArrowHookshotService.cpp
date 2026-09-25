// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "ArrowHookshotService.hpp"

#include "ActorReference.hpp"
#include "ArrowIdentity.hpp"
#include "ArrowImpactAdapter.hpp"
#include "ArrowModeService.hpp"
#include "HookshotAudio.hpp"
#include "HookshotLog.hpp"
#include "HookshotWorld.hpp"
#include "FlightDiagnostics.hpp"
#include "../../../pure/FlightDiagnostics.hpp"
#include "WallGripService.hpp"
#include "../../../engine/FlightClock.hpp"
#include "../../../engine/RagdollTransport.hpp"
#include "totk/engine/Pointer.hpp"
#include "totk/engine/Totk121Offsets.hpp"

namespace arrowbound::arrow_hookshot {
namespace {
using namespace arrowbound::pure;

constexpr std::ptrdiff_t kEquipmentIsPouchUser = 0x00CDD494;
constexpr std::ptrdiff_t kActorLinkGetReference = 0x00753530;
constexpr auto kControllerActor = engine::kArrowControllerActor;
constexpr std::ptrdiff_t kBodyGetNextVelocity = 0x011B44AC;
// Native update 0x0173B260 reads this rate; flight/fall velocity includes its multiplier.
constexpr std::ptrdiff_t kActorArrowTimeRate = 0x280;
constexpr std::ptrdiff_t kControllerState = 368;
constexpr ArrowConfig kConfig{};

bool okPtr(std::uintptr_t value) { return totk::engine::isPlausibleAddress(value); }

bool readArrowBodyMotion(std::uintptr_t actor, Vec3& position, Vec3& velocity) {
    using SearchBodyFn = std::uintptr_t (*)(std::uintptr_t, const char* const*,
                                            const char* const*);
    using GetPositionFn = float (*)(std::uintptr_t, Vec3*);
    const char* controllerName = "Atk";
    const char* bodyName = "Bullet";
    const auto search = reinterpret_cast<SearchBodyFn>(
        runtime().session.base +
        totk::engine::Totk121Offsets::kActorSearchRigidBodySensor.value);
    const auto body = search(actor, &controllerName, &bodyName);
    if (!okPtr(body)) return false;
    const auto getPosition = reinterpret_cast<GetPositionFn>(
        runtime().session.base +
        totk::engine::Totk121Offsets::kRigidBodyGetPosition.value);
    getPosition(body, &position);
    using GetVelocityFn = void (*)(std::uintptr_t, Vec3*);
    reinterpret_cast<GetVelocityFn>(runtime().session.base + kBodyGetNextVelocity)(body, &velocity);
    return finite3(position) && finite3(velocity);
}

engine::ArrowIdentity arrowIdentity(void* rawController) {
    using GetReferenceFn = engine::ActorReference (*)(std::uintptr_t);
    const auto getReference = reinterpret_cast<GetReferenceFn>(
        runtime().session.base + kActorLinkGetReference);
    return engine::resolveArrowIdentity(reinterpret_cast<std::uintptr_t>(rawController), okPtr,
        [](std::uintptr_t address) { return *reinterpret_cast<const std::uintptr_t*>(address); },
        [getReference](std::uintptr_t link) {
            const auto reference = getReference(link);
            return reference.actor;
        });
}

void retireToken(HookshotRuntime& rt) {
    rt.arrow.pendingShot.store(0, std::memory_order_release);
    rt.arrow.controllerToken.store(0, std::memory_order_release);
}

void stopDrive(HookshotRuntime& rt) {
    wall_grip::stop(rt);
    rt.drive.active.store(0, std::memory_order_release);
    rt.drive.forceEntry.store(0, std::memory_order_release);
    rt.drive.admissionWanted.store(0, std::memory_order_release);
    rt.drive.captureActive.store(0, std::memory_order_release);
    rt.drive.presentParaglider.store(0, std::memory_order_release);
}

void armParasail(HookshotRuntime& rt) {
    rt.drive.active.store(0, std::memory_order_release);
    rt.drive.captureActive.store(0, std::memory_order_release);
    rt.drive.admissionWanted.store(1, std::memory_order_release);
    if (!rt.drive.parasailActive.load(std::memory_order_acquire)) {
        rt.drive.admissionArms.fetch_add(1, std::memory_order_relaxed);
        rt.drive.forceEntry.store(1, std::memory_order_release);
    }
}

bool beginFollow(HookshotRuntime& rt, Vec3 position, Vec3 velocity) {
    if (!world::readPlayerRotation(rt.arrowTrip.rotation)) return false;
    rt.arrow.followBegins.fetch_add(1,std::memory_order_relaxed);
    rt.arrowTrip.rotationValid = true;
    rt.arrowTrip.arrowPosition = position;
    rt.arrowTrip.arrowVelocity = velocity;
    rt.arrowTrip.follower = {};
    rt.arrowTrip.phase = ArrowPhase::Following;
    rt.arrowTrip.phaseTick = rt.arrowTrip.lastSampleTick = rt.session.tick;
    rt.drive.presentParaglider.store(1, std::memory_order_release);
    armParasail(rt);
    audio::startArrowFollowLoop(world::playerPosition());
    note("following arrow; paraglider requested; B releases");
    ZHLOG("ARROW_FOLLOW_BEGIN shot=%u parasail=%u fall=%u",
          rt.arrowTrip.shotSeqSeen,
          rt.drive.parasailActive.load(std::memory_order_relaxed),
          rt.drive.fallActive.load(std::memory_order_relaxed));
    ZHLOG("ARROW_FOLLOW_MOTION speed_cm_s=%d deferred=%u",
          (int)(length(velocity) * 100.0f),
          rt.arrow.sampleMotionWaits.load(std::memory_order_relaxed));
    return true;
}

void enterBailout(HookshotRuntime& rt, const char* reason) {
    if (rt.arrowTrip.phase == ArrowPhase::Bailout) return;
    const bool followed = rt.arrowTrip.phase == ArrowPhase::Following;
    wall_grip::stop(rt);
    retireToken(rt);
    rt.arrowTrip.phase = ArrowPhase::Bailout;
    rt.arrowTrip.phaseTick = rt.session.tick;
    rt.arrowTrip.bailoutGlideUpdates = rt.drive.updates.load(std::memory_order_acquire);
    audio::stopArrowFollowLoop();
    if (followed) audio::playAbilityCue(true, world::playerPosition());
    rt.drive.presentParaglider.store(1, std::memory_order_release);
    armParasail(rt);
    ZHLOG("ARROW_BAILOUT reason=%s", reason);
    if (followed)
        ZHLOG("ARROW_FOLLOW_END max_accept_error_cm=%d",
              (int)(rt.arrowTrip.maxAcceptanceError * 100.0f));
    note("arrow flight ended; opening the paraglider");
}

void finishBailout(HookshotRuntime& rt, const char* reason) {
    const auto updatesAtBailout = rt.arrowTrip.bailoutGlideUpdates;
    stopDrive(rt);
    retireArrowTrip(rt.arrowTrip, rt.arrow.shotSeq.load(std::memory_order_acquire));
    ZHLOG("ARROW_BAILOUT_DONE reason=%s parasail=%u glide_updates=%u start_updates=%u", reason,
          rt.drive.parasailActive.load(std::memory_order_relaxed),
          rt.drive.updates.load(std::memory_order_relaxed), updatesAtBailout);
}
}  // namespace

void onArrowRelease(void* equipmentUser) {
    auto& rt = runtime();
    auto& mailbox = rt.arrow;
    if (!equipmentUser) return;
#if !ARROWBOUND_FLIGHT_DIAGNOSTICS
    if (mailbox.modeEnabled.load(std::memory_order_acquire) == 0 ||
        mailbox.acceptShots.load(std::memory_order_acquire) == 0) return;
#endif
    using IsPouchUserFn = std::uint64_t (*)(void*);
    const auto isPouchUser = reinterpret_cast<IsPouchUserFn>(rt.session.base + kEquipmentIsPouchUser);
    if ((isPouchUser(equipmentUser) & 1u) == 0) return;
    mailbox.releaseObserved.fetch_add(1,std::memory_order_relaxed);
    diagnostics::release(mailbox.modeEnabled.load(std::memory_order_acquire) != 0);
    if (mailbox.modeEnabled.load(std::memory_order_acquire) == 0 ||
        !mailbox.acceptShots.exchange(0, std::memory_order_acq_rel)) return;
    mailbox.controllerToken.store(0, std::memory_order_release);
    mailbox.claimOldIgnored.store(0, std::memory_order_relaxed);
    mailbox.claimOwnerMisses.store(0, std::memory_order_relaxed);
    mailbox.sampleBodyMisses.store(0, std::memory_order_relaxed);
    mailbox.sampleMotionWaits.store(0, std::memory_order_relaxed);
    mailbox.impactChecks.store(0, std::memory_order_relaxed);
    mailbox.impactPublished.store(0, std::memory_order_release);
    mailbox.shotSampleBase.store(mailbox.sampleSeq.load(std::memory_order_acquire),
                                std::memory_order_relaxed);
    mailbox.shotHitBase.store(mailbox.hitSeq.load(std::memory_order_acquire),
                             std::memory_order_relaxed);
    const auto seq = mailbox.shotSeq.fetch_add(1, std::memory_order_acq_rel) + 1;
    mailbox.pendingShot.store(1, std::memory_order_release);
    const bool stepRateReset = world::resetPlayerStepRate();
    ZHLOG("ARROW_RELEASE seq=%u live_follow=1 step_rate_reset=%u", seq,
          (unsigned)stepRateReset);
    if (!stepRateReset) note("arrow released; slow-motion reset was unavailable");
}

void onArrowUpdate(void* controller) {
    auto& mailbox = runtime().arrow;
#if ARROWBOUND_FLIGHT_DIAGNOSTICS
    const auto diagnosticController = reinterpret_cast<std::uintptr_t>(controller);
    if (okPtr(diagnosticController)) {
        const auto diagnosticState = *reinterpret_cast<const std::uint32_t*>(diagnosticController + kControllerState);
        if (arrowClaimIsNew(diagnosticState)) {
            const auto player = mailbox.playerActor.load(std::memory_order_acquire);
            diagnostics::claim(controller, diagnosticState, okPtr(player) && arrowIdentity(controller).belongsTo(player));
        }
    }
#endif
    if (mailbox.controllerToken.load(std::memory_order_acquire) ||
        !mailbox.pendingShot.load(std::memory_order_acquire)) return;
    const auto candidate = reinterpret_cast<std::uintptr_t>(controller);
    if (!okPtr(candidate)) return;
    const auto state = *reinterpret_cast<const std::uint32_t*>(candidate + kControllerState);
    if (!arrowClaimIsNew(state)) {
        if (mailbox.claimOldIgnored.fetch_add(1, std::memory_order_relaxed) == 0)
            ZHLOG("ARROW_CLAIM_SKIP state=%u reason=not_new", state);
        return;
    }
    const auto player = mailbox.playerActor.load(std::memory_order_acquire);
    const auto identity = arrowIdentity(controller);
    if (!okPtr(player) || !identity.belongsTo(player)) {
        if (mailbox.claimOwnerMisses.fetch_add(1, std::memory_order_relaxed) == 0)
            ZHLOG("ARROW_OWNER_MISS state=%u shooter=%p player=%p", state,
                  reinterpret_cast<void*>(identity.shooter), reinterpret_cast<void*>(player));
        return;
    }
    std::uintptr_t expected = 0;
    if (mailbox.controllerToken.compare_exchange_strong(expected, candidate, std::memory_order_acq_rel)) {
        mailbox.pendingShot.store(0, std::memory_order_release);
        ZHLOG("ARROW_CLAIM controller=%p state=%u native_motion=1 shot=%u", controller, state,
              mailbox.shotSeq.load(std::memory_order_relaxed));
    }
}

bool isTracked(void* controller) {
    const auto& mailbox = runtime().arrow;
    return controller && mailbox.controllerToken.load(std::memory_order_acquire) ==
                             reinterpret_cast<std::uintptr_t>(controller) &&
           mailbox.impactPublished.load(std::memory_order_acquire) == 0;
}

void onArrowSample(void* rawController, float nativeDelta) {
    if (!isTracked(rawController) && !diagnostics::observes(rawController)) return;
    const auto controller = reinterpret_cast<std::uintptr_t>(rawController);
    const auto actor = *reinterpret_cast<const std::uintptr_t*>(controller + kControllerActor);
    if (!okPtr(actor)) return;
    Vec3 position{}, velocity{};
    if (!readArrowBodyMotion(actor, position, velocity)) {
        auto& misses = runtime().arrow.sampleBodyMisses;
        if (misses.fetch_add(1, std::memory_order_relaxed) == 0)
            ZHLOG("ARROW_BODY_POSITION_MISS actor=%p", reinterpret_cast<void*>(actor));
        return;
    }
    auto& mailbox = runtime().arrow;
    const Vec3 bodyVelocity = velocity;
    const float actorRate = *reinterpret_cast<const float*>(actor + kActorArrowTimeRate);
    diagnostics::sample(rawController, position, bodyVelocity, actorRate, nativeDelta);
    if (!isTracked(rawController)) return;
    if (!arrowFollowVelocity(bodyVelocity, actorRate, velocity)) {
        ZHLOG("ARROW_SAMPLE_CLOCK_INVALID shot=%u rate_bits=%08x",
              mailbox.shotSeq.load(std::memory_order_relaxed), floatToBits(actorRate));
        return;
    }
    // The first native update can return before writing Bullet's launch velocity.
    if (!arrowMotionReady(position, velocity)) {
        if (mailbox.sampleMotionWaits.fetch_add(1, std::memory_order_relaxed) == 0)
            ZHLOG("ARROW_SAMPLE_WAIT state=%u reason=motion_not_ready",
                  *reinterpret_cast<const std::uint32_t*>(controller + kControllerState));
        return;
    }
    mailbox.sampleSeq.fetch_add(1, std::memory_order_acq_rel);
    mailbox.samplePosition.store(position);
    mailbox.sampleVelocity.store(velocity);
    const auto published = mailbox.sampleSeq.fetch_add(1, std::memory_order_release) + 1;
    const auto sample = (published - mailbox.shotSampleBase.load(std::memory_order_relaxed)) / 2;
    if (sample <= 8 || sample % 60 == 0)
        ZHLOG("ARROW_SAMPLE_CLOCK shot=%u sample=%u rate_milli=%d raw_speed_cm_s=%d follow_speed_cm_s=%d",
              mailbox.shotSeq.load(std::memory_order_relaxed), sample,
              (int)(actorRate * 1000.0f), (int)(length(bodyVelocity) * 100.0f),
              (int)(length(velocity) * 100.0f));
}

void onNativeImpact(void* rawController, bool hit, ArrowImpact impact) {
    if (!isTracked(rawController)) return;
    auto& mailbox = runtime().arrow;
    const auto checks = mailbox.impactChecks.fetch_add(1, std::memory_order_relaxed) + 1;
    if (!hit) {
        if (checks == 1) ZHLOG("ARROW_IMPACT_WAIT native_checks=1");
        return;
    }
    const auto result = publishArrowImpact(hit, impact, mailbox.impactPublished, [&](const ArrowImpact& actual) {
        mailbox.hitPosition.store(actual.contact);
        mailbox.hitDirection.store(actual.direction);
        mailbox.hitWallCandidate.store(actual.wallCandidate() ? 1u : 0u, std::memory_order_relaxed);
        mailbox.hitSeq.fetch_add(1, std::memory_order_release);
    });
    ZHLOG("ARROW_IMPACT source=%u type=%d water=%u publication=%u checks=%u pos_dm=(%d,%d,%d)",
          (unsigned)impact.source, impact.sensorType, (unsigned)impact.water, (unsigned)result, checks,
          finite3(impact.contact) ? (int)(impact.contact.x * 10) : 0,
          finite3(impact.contact) ? (int)(impact.contact.y * 10) : 0,
          finite3(impact.contact) ? (int)(impact.contact.z * 10) : 0);
}

void onArrowImpact(void* rawController, bool classified, int hitType,
                   const float* adjustedHit, const void* motionContext) {
    if (!isTracked(rawController) && !diagnostics::observes(rawController)) return;
    ArrowImpact impact{};
    impact.sensorType = hitType;
    if (classified) impact = engine::nativeArrowImpact(rawController, ArrowImpactSource::Sensor,
        hitType, false, adjustedHit, motionContext);
    if (classified) diagnostics::impact(rawController, impact);
    onNativeImpact(rawController, classified, impact);
}

void onArrowWorldImpact(void* rawController, bool hit, bool water) {
    if (!isTracked(rawController) && !diagnostics::observes(rawController)) return;
    ArrowImpact impact{};
    impact.source = ArrowImpactSource::WorldSweep;
    impact.water = water;
    if (hit) impact = engine::nativeArrowImpact(rawController, ArrowImpactSource::WorldSweep, 0, water);
    if (hit) diagnostics::impact(rawController, impact);
    onNativeImpact(rawController, hit, impact);
}

void service(HookshotRuntime& rt, bool cancel, bool reaim, bool allowShots) {
    auto& trip = rt.arrowTrip;
    auto& mailbox = rt.arrow;
    const bool enabled = arrow_mode::enabled();
    diagnostics::gameplay(rt);
    ragdoll_transport::retireIfInactive();
    // A host reservation also retires a release that arrived before its next tick.
    if (!allowShots) {
        mailbox.acceptShots.store(0, std::memory_order_release);
        if (arrowHasFlight(trip.phase, mailbox.shotSeq.load(std::memory_order_acquire),
                           trip.shotSeqSeen)) {
            reset(rt);
            ZHLOG("ARROW_DETACH reason=host_reservation");
        }
    }
    mailbox.playerActor.store(reinterpret_cast<std::uintptr_t>(world::playerActor()), std::memory_order_release);
    mailbox.modeEnabled.store(enabled ? 1u : 0u, std::memory_order_release);
    if (reaim && arrowHasFlight(trip.phase,
                               mailbox.shotSeq.load(std::memory_order_acquire), trip.shotSeqSeen)) {
        const auto previousShot = trip.shotSeqSeen;
        retireToken(rt);
        audio::stopArrowFollowLoop();
        stopDrive(rt);
        retireArrowTrip(trip, mailbox.shotSeq.load(std::memory_order_acquire));
        ZHLOG("ARROW_DETACH reason=bow_aim shot=%u", previousShot);
    }
    mailbox.acceptShots.store(allowShots && enabled && world::ready() && trip.phase == ArrowPhase::Idle &&
        !mailbox.pendingShot.load(std::memory_order_acquire),
        std::memory_order_release);

    if ((!enabled || !world::ready()) &&
        trip.phase != ArrowPhase::Idle && trip.phase != ArrowPhase::Bailout) {
        enterBailout(rt, "mode/world/manual state changed");
    }
    const auto shotSeq = mailbox.shotSeq.load(std::memory_order_acquire);
    if (trip.phase == ArrowPhase::Idle && allowShots && enabled && world::ready() &&
        shotSeq != trip.shotSeqSeen) {
        trip.phase = ArrowPhase::WaitingForArrow;
        trip.phaseTick = trip.lastSampleTick = rt.session.tick;
        trip.shotSeqSeen = shotSeq;
        trip.sampleSeqSeen = mailbox.shotSampleBase.load(std::memory_order_acquire);
        trip.hitSeqSeen = mailbox.shotHitBase.load(std::memory_order_acquire);
        mailbox.acceptShots.store(0, std::memory_order_release);
    }
    if (trip.phase == ArrowPhase::Idle) return;
    if (cancel) {
        enterBailout(rt, "player cancel");
    }

    bool freshSample = false;
    const auto sampleSeq = mailbox.sampleSeq.load(std::memory_order_acquire);
    if (!(sampleSeq & 1u) && sampleSeq != trip.sampleSeqSeen &&
        (trip.phase == ArrowPhase::WaitingForArrow || trip.phase == ArrowPhase::Following)) {
        const auto position = mailbox.samplePosition.load();
        const auto velocity = mailbox.sampleVelocity.load();
        std::atomic_thread_fence(std::memory_order_acquire);
        if (mailbox.sampleSeq.load(std::memory_order_acquire) == sampleSeq) {
            freshSample = true;
            trip.pendingMotionSample = true;
            trip.sampleSeqSeen = sampleSeq;
            trip.arrowPosition = position;
            trip.arrowVelocity = velocity;
            trip.lastSampleTick = rt.session.tick;
            if (trip.phase == ArrowPhase::WaitingForArrow &&
                !beginFollow(rt, position, velocity))
                enterBailout(rt, "player basis unavailable");
        }
    }
    const auto hitSeq = mailbox.hitSeq.load(std::memory_order_acquire);
    if (hitSeq != trip.hitSeqSeen &&
        (trip.phase == ArrowPhase::WaitingForArrow || trip.phase == ArrowPhase::Following)) {
        trip.hitSeqSeen = hitSeq;
        if (trip.phase == ArrowPhase::WaitingForArrow &&
            !beginFollow(rt, mailbox.hitPosition.load(), mailbox.hitDirection.load()))
            enterBailout(rt, "player basis unavailable at impact");
        Vec3 target{};
        if (trip.phase == ArrowPhase::Following && trip.rotationValid &&
            arrowTrailPoint(mailbox.hitPosition.load(), mailbox.hitDirection.load(), target, kConfig))
            world::forcePlayerPose(trip.rotation, target);
        if (trip.phase == ArrowPhase::Following) {
            if (mailbox.hitWallCandidate.load(std::memory_order_relaxed) &&
                wall_grip::begin(rt, mailbox.hitPosition.load(), mailbox.hitDirection.load())) {
                retireToken(rt);
                audio::stopArrowFollowLoop();
                audio::playAbilityCue(true, world::playerPosition());
            } else {
                enterBailout(rt, "arrow impact");
            }
        }
    } else if (trip.phase == ArrowPhase::Following) {
        if (arrowTimedOut(rt.session.tick, trip.lastSampleTick,
                          kConfig.updateTimeoutTicks)) {
            enterBailout(rt, "arrow ended or stopped updating");
        } else {
            Vec3 target{}, followPosition{}, followVelocity{};
            pure::FlightTime clock{};
            float elapsed = 0;
            // Contention skips a callback, not elapsed time; the next snapshot contains the total.
            if (!game_clock::snapshot(clock)) return;
            if (!trip.clock.step(clock, elapsed)) {
                mailbox.clockRejects.fetch_add(1,std::memory_order_relaxed);
                ZHLOG("ARROW_CLOCK_REJECT status=%u serial=%llu", unsigned(clock.status),
                      (unsigned long long)clock.serial);
                enterBailout(rt, "simulation clock unavailable");
                return;
            }
            if (elapsed == 0 && trip.haveRequestedPosition) return;
            if (!trip.prediction.step(freshSample || trip.pendingMotionSample,
                                      trip.arrowVelocity, elapsed)) {
                ZHLOG("ARROW_PREDICTION_LIMIT shot=%u sample_age=%llu stale_us=%d predicted_cm=%d step_us=%d speed_cm_s=%d",
                      trip.shotSeqSeen, (unsigned long long)(rt.session.tick-trip.lastSampleTick),
                      traceNumber(trip.prediction.seconds(),1000000),
                      traceNumber(trip.prediction.distance()), traceNumber(elapsed,1000000),
                      traceNumber(length(trip.arrowVelocity)));
                enterBailout(rt, "arrow prediction budget exceeded");
                return;
            }
            const float acceptanceError = trip.haveRequestedPosition ?
                distance(world::playerPosition(), trip.lastRequestedPosition) : 0;
            if (std::isfinite(acceptanceError) && acceptanceError > trip.maxAcceptanceError)
                trip.maxAcceptanceError = acceptanceError;
            if (!trip.rotationValid)
                enterBailout(rt, "player rotation unavailable");
            else if (!trip.follower.update(rt.session.tick, trip.arrowPosition,
                                      trip.arrowVelocity, freshSample || trip.pendingMotionSample,
                                      followPosition, followVelocity, kConfig, elapsed))
                enterBailout(rt, "follower sample rejected");
            else if (!arrowTrailPoint(followPosition, followVelocity, target, kConfig))
                enterBailout(rt, "arrow trail direction unavailable");
            else if (!world::forcePlayerPose(trip.rotation, target))
                enterBailout(rt, "carrier write refused");
            else {
                trip.pendingMotionSample = false;
                mailbox.carrierWrites.fetch_add(1,std::memory_order_relaxed);
                const float requestedStep = trip.haveRequestedPosition ?
                    distance(target, trip.lastRequestedPosition) : 0;
                const auto followTick = rt.session.tick - trip.phaseTick;
                if (ARROWBOUND_FLIGHT_DIAGNOSTICS && clock.serial % 6 == 0)
                    ZHLOG("TRACE_FOLLOW_CLOCK shot=%u serial=%llu ns=%llu dt_us=%d scale_milli=%d",
                          trip.shotSeqSeen, (unsigned long long)clock.serial,
                          (unsigned long long)clock.nanoseconds, traceNumber(elapsed,1000000),
                          traceNumber(clock.scale,1000));
                if (followTick < 8 || followTick % 120 == 0 ||
                    (ARROWBOUND_FLIGHT_DIAGNOSTICS && clock.serial % 6 == 0))
                    ZHLOG("ARROW_FOLLOW_SAMPLE tick=%llu body_error_cm=%d step_cm=%d correction_cm=%d accept_error_cm=%d sample_age=%llu",
                      (unsigned long long)(rt.session.tick - trip.phaseTick),
                      traceNumber(distance(followPosition, trip.arrowPosition)),
                      traceNumber(requestedStep),
                      traceNumber(trip.follower.correctionDistance()),
                      traceNumber(acceptanceError),
                      (unsigned long long)(rt.session.tick - trip.lastSampleTick));
                trip.lastRequestedPosition = target;
                trip.haveRequestedPosition = true;
            }
        }
    } else if (trip.phase == ArrowPhase::WaitingForArrow &&
               arrowTimedOut(rt.session.tick, trip.phaseTick, kConfig.claimTimeoutTicks)) {
        ZHLOG("ARROW_CLAIM_TIMEOUT old_ignored=%u owner_misses=%u",
              mailbox.claimOldIgnored.load(std::memory_order_relaxed),
              mailbox.claimOwnerMisses.load(std::memory_order_relaxed));
        enterBailout(rt, "arrow claim timeout");
    }

    if (trip.phase == ArrowPhase::WallProbe || trip.phase == ArrowPhase::WallGrip) {
        if (const char* result = wall_grip::service(rt)) {
            if (std::strcmp(result, "climb") == 0) {
                ZHLOG("WALL_GRIP_SUCCESS shot=%u applied=%u input=%u", trip.shotSeqSeen,
                      rt.grip.applied.load(std::memory_order_relaxed),
                      rt.grip.inputApplied.load(std::memory_order_relaxed));
                stopDrive(rt);
                retireToken(rt);
                retireArrowTrip(trip, mailbox.shotSeq.load(std::memory_order_acquire));
                note("wall gripped; normal climbing controls restored");
            } else {
                ZHLOG("WALL_GRIP_END shot=%u reason=%s", trip.shotSeqSeen, result);
                enterBailout(rt, result);
            }
        }
    }

    if (trip.phase == ArrowPhase::Bailout) {
        if (arrowBailoutReady(rt.drive.parasailActive.load(std::memory_order_acquire) != 0,
                             rt.drive.updates.load(std::memory_order_acquire),
                             trip.bailoutGlideUpdates))
            finishBailout(rt, "paraglider updated after release");
        else if (arrowTimedOut(rt.session.tick, trip.phaseTick, kConfig.bailoutTimeoutTicks))
            finishBailout(rt, "paraglider timeout");
        else if (rt.drive.fallActive.load(std::memory_order_acquire))
            rt.drive.forceEntry.store(1, std::memory_order_release);
    }
}

void reset(HookshotRuntime& rt) {
    audio::stopArrowFollowLoop();
    stopDrive(rt);
    retireArrowTrip(rt.arrowTrip, rt.arrow.shotSeq.load(std::memory_order_acquire));
    rt.arrow.playerActor.store(0, std::memory_order_release);
    rt.arrow.acceptShots.store(0, std::memory_order_release);
    rt.arrow.impactPublished.store(0, std::memory_order_release);
    retireToken(rt);
}

bool engaged(const HookshotRuntime& rt) { return rt.arrowTrip.phase != ArrowPhase::Idle; }

bool keepsParagliderPresented() {
    return runtime().drive.presentParaglider.load(std::memory_order_acquire) != 0;
}

}  // namespace arrowbound::arrow_hookshot
