// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "PlacementCoordinator.hpp"

#include "PlacementPolicy.hpp"

#include <lib.hpp>

namespace bivouac::feature {
namespace {

#define BVLOG(...) Logging.Log("[bv] " __VA_ARGS__)

float squareRoot(float value) {
    return value > 0.0f ? __builtin_sqrtf(value) : 0.0f;
}

int centimeters(float meters) {
    return static_cast<int>(
        meters * 100.0f + (meters >= 0.0f ? 0.5f : -0.5f));
}

PlacementOutcome abort(const char* reason, bool refund) {
    return {
        PlacementOutcomeKind::Abort,
        reason,
        refund,
        {},
    };
}

PlacementOutcome commit(const PlacementCandidate& candidate) {
    return {
        PlacementOutcomeKind::Commit,
        nullptr,
        false,
        candidate,
    };
}

} // namespace

void PlacementCoordinator::requestCast(
    const runtime::Vec3& from, const runtime::Vec3& to,
    int currentTick) {
    const float nativeFrom[3] = {from.x, from.y, from.z};
    const float nativeTo[3] = {to.x, to.y, to.z};
    mRaycasts.request(
        nativeFrom, nativeTo, mTuning.collisionMask, currentTick);
}

PlacementCoordinator::CastResult PlacementCoordinator::pollCast(
    int currentTick) {
    const engine::RaycastResult result =
        mRaycasts.poll(currentTick, mTuning.castTimeoutTicks);
    return {
        result.resolved,
        result.timedOut,
        result.hit,
        result.distance,
        {result.position[0], result.position[1], result.position[2]},
        {result.normal[0], result.normal[1], result.normal[2]},
        result.bodyId,
    };
}

void PlacementCoordinator::reset(
    runtime::PlacementState& placement) {
    placement.placeLane = bivouac::placement::Lane::Cliff;
    placement.placeStep = runtime::PlaceStep::IDLE;
    placement.motherBaseFallbackFromCliff = false;
    placement.motherBasePlacement.reset();
    mTerrainWater.begin(0);
    mRaycasts.cancel();
}

void PlacementCoordinator::fallbackToMotherBase(
    runtime::PlacementState& placement,
    runtime::TriggerState& trigger,
    const char* reason,
    int currentTick) {
    BVLOG("cliff FALLBACK -> Mother Base: %s (failed fallback retains cliff-intent refund)",
          reason);
    placement.placeLane = bivouac::placement::Lane::MotherBase;
    placement.placeStep = runtime::PlaceStep::IDLE;
    trigger.trigArmedTick = currentTick;
    placement.motherBaseFallbackFromCliff = true;
    placement.motherBasePlacement.reset();
    mTerrainWater.begin(0);
    mRaycasts.cancel();
}

int PlacementCoordinator::nearestSite(
    const runtime::CampCollection& camps,
    const runtime::Vec3& position,
    float* distance) const {
    int nearestIndex = -1;
    float nearestDistanceSquared = 1e30f;
    for (int siteIndex = 0;
         siteIndex < runtime::kMaximumSites; ++siteIndex) {
        const runtime::Site& site = camps.sites[siteIndex];
        if (!site.active) {
            continue;
        }
        const float dx = site.anchor.x - position.x;
        const float dy = site.anchor.y - position.y;
        const float dz = site.anchor.z - position.z;
        const float distanceSquared = dx * dx + dy * dy + dz * dz;
        if (distanceSquared < nearestDistanceSquared) {
            nearestDistanceSquared = distanceSquared;
            nearestIndex = siteIndex;
        }
    }
    if (nearestIndex >= 0 && distance != nullptr) {
        *distance = squareRoot(nearestDistanceSquared);
    }
    return nearestIndex;
}

PlacementOutcome PlacementCoordinator::motherBaseTick(
    runtime::PlacementState& state,
    const runtime::CampCollection& camps,
    std::uintptr_t mainBase,
    const runtime::Vec3& playerPosition,
    const runtime::Vec3& playerForward,
    int currentTick) {
    motherbase::Placement& placement = state.motherBasePlacement;
    if (placement.phase() == motherbase::Phase::Idle) {
        mTerrainWater.begin(mainBase);
        placement.begin(
            {playerPosition.x, playerPosition.y, playerPosition.z},
            playerForward.x, playerForward.z);
        BVLOG("Mother Base: searching from Link (%d,%d,%d)cm facing=(%d,%d)/1000",
              centimeters(playerPosition.x),
              centimeters(playerPosition.y),
              centimeters(playerPosition.z),
              static_cast<int>(playerForward.x * 1000.0f),
              static_cast<int>(playerForward.z * 1000.0f));
    }

    if (placement.phase() == motherbase::Phase::ScanAhead
        || placement.phase()
            == motherbase::Phase::ValidateFootprint) {
        placement.advanceWater(
            &engine::TerrainWaterService::query,
            &mTerrainWater,
            mTuning.motherBaseWaterQueriesPerTick);
    }

    if (placement.phase() == motherbase::Phase::ValidateIntent) {
        if (mRaycasts.castBusy()) {
            const CastResult result = pollCast(currentTick);
            if (!result.resolved) {
                return {};
            }
            placement.acceptIntentResult(
                result.timedOut, result.hit != 0,
                result.position.y, result.bodyId);
        }
        if (placement.phase() == motherbase::Phase::ValidateIntent
            && !mRaycasts.castBusy()
            && placement.wantsIntentRay()) {
            const motherbase::IntentRay ray = placement.intentRay();
            BVLOG("Mother Base: visibility ray (%d,%d,%d)->(%d,%d,%d)cm surfaceDelta=%dcm",
                  centimeters(ray.from.x), centimeters(ray.from.y),
                  centimeters(ray.from.z), centimeters(ray.to.x),
                  centimeters(ray.to.y), centimeters(ray.to.z),
                  centimeters(placement.intentSurfaceDeltaM()));
            requestCast(
                {ray.from.x, ray.from.y, ray.from.z},
                {ray.to.x, ray.to.y, ray.to.z},
                currentTick);
            return {};
        }
    }

    if (placement.phase()
        == motherbase::Phase::ValidateClearance) {
        if (mRaycasts.castBusy()) {
            const CastResult result = pollCast(currentTick);
            if (!result.resolved) {
                return {};
            }
            placement.acceptClearanceResult(
                result.timedOut, result.hit != 0,
                result.position.y, result.bodyId);
        }
        if (placement.phase()
                == motherbase::Phase::ValidateClearance
            && !mRaycasts.castBusy()
            && placement.wantsClearanceRay()) {
            const motherbase::ClearanceRay ray =
                placement.clearanceRay();
            requestCast(
                {ray.from.x, ray.from.y, ray.from.z},
                {ray.to.x, ray.to.y, ray.to.z},
                currentTick);
            return {};
        }
    }

    if (placement.phase() == motherbase::Phase::Rejected) {
        const motherbase::RejectReason reason =
            placement.rejectReason();
        const bool refund =
            motherbase::shouldRefundPlacementFailure(
                placement.shouldRefund(),
                state.motherBaseFallbackFromCliff);
        BVLOG("Mother Base REJECT: %s refund=%d sawWater=%d visibleIntent=%d attempts=%u waterSamples=%u clearanceSamples=%u lastCode=%d failedDepth=%dcm surfaceDelta=%dcm intentDelta=%dcm intentBody=%x intentY=%dcm obstructionBody=%x obstructionY=%dcm",
              motherbase::rejectReasonName(reason),
              refund ? 1 : 0,
              placement.sawWater() ? 1 : 0,
              placement.visibleWaterIntent() ? 1 : 0,
              static_cast<std::uint32_t>(
                  placement.candidateAttempts()),
              static_cast<std::uint32_t>(
                  placement.waterSamplesTested()),
              static_cast<std::uint32_t>(
                  placement.clearanceSamplesTested()),
              placement.lastNativeCode(),
              centimeters(placement.failedSampleDepthM()),
              centimeters(placement.failedSampleSurfaceDeltaM()),
              centimeters(placement.intentSurfaceDeltaM()),
              placement.intentBodyId(),
              centimeters(placement.intentHitY()),
              placement.obstructionBodyId(),
              centimeters(placement.obstructionHitY()));
        return abort(motherbase::rejectReasonName(reason), refund);
    }

    if (placement.phase() != motherbase::Phase::Ready) {
        return {};
    }

    const motherbase::ReadySite ready = placement.readySite();
    float separation = 0.0f;
    const runtime::Vec3 candidatePosition{
        ready.anchor.x, ready.anchor.y, ready.anchor.z};
    const int nearest =
        nearestSite(camps, candidatePosition, &separation);
    if (nearest >= 0
        && separation < mTuning.motherBaseSiteSeparation) {
        BVLOG("Mother Base REJECT: candidate %dcm from site#%u (min %dcm)",
              centimeters(separation), camps.sites[nearest].seq,
              centimeters(mTuning.motherBaseSiteSeparation));
        return abort(
            "Mother Base would overlap an existing site", true);
    }

    PlacementCandidate candidate{};
    candidate.anchor = candidatePosition;
    candidate.normalX = ready.nx;
    candidate.normalZ = ready.nz;
    candidate.waterMode = true;
    candidate.anchorDistance = ready.anchorDistanceM;
    candidate.minimumWaterDepth = ready.minDepthM;
    candidate.maximumWaterDepth = ready.maxDepthM;
    candidate.waterSamples = placement.waterSamplesTested();
    candidate.clearanceSamples =
        placement.clearanceSamplesTested();
    candidate.candidateAttempts =
        placement.candidateAttempts();
    return commit(candidate);
}

PlacementOutcome PlacementCoordinator::cliffTick(
    runtime::PlacementState& state,
    runtime::TriggerState& trigger,
    const runtime::Vec3& playerPosition,
    const runtime::Vec3& playerForward,
    int currentTick) {
    switch (state.placeStep) {
    case runtime::PlaceStep::IDLE:
        state.wallRetries = 0;
        state.placeGapFloor = 0.0f;
        state.placeGapRoof = 0.0f;
        state.sweepRay = 0;
        state.sweepHitsDeck = 0;
        state.sweepHitsRoof = 0;
        state.sweepSeat = 0.0f;
        state.sweepMinDeck = 0.0f;
        state.sweepMinRoof = 0.0f;
        for (int index = 0;
             index < 2 * mTuning.sweepRayCount; ++index) {
            state.sweepDepth[index] =
                mTuning.sweepDiagnosticMiss;
        }
        state.placeStep = runtime::PlaceStep::WALL;
        [[fallthrough]];

    case runtime::PlaceStep::WALL: {
        if (!mRaycasts.castBusy()) {
            const runtime::Vec3 origin{
                playerPosition.x
                    - playerForward.x * mTuning.wallCastBackoff,
                playerPosition.y + 1.0f
                    - playerForward.y * mTuning.wallCastBackoff,
                playerPosition.z
                    - playerForward.z * mTuning.wallCastBackoff,
            };
            requestCast(
                origin,
                {
                    origin.x
                        + playerForward.x * mTuning.wallCastLength,
                    origin.y
                        + playerForward.y * mTuning.wallCastLength,
                    origin.z
                        + playerForward.z * mTuning.wallCastLength,
                },
                currentTick);
            return {};
        }
        const CastResult result = pollCast(currentTick);
        if (!result.resolved) {
            return {};
        }
        if (result.timedOut || result.hit == 0) {
            if (bivouac::placement::wallRetriesExhausted(
                    ++state.wallRetries,
                    mTuning.wallCastRetries)) {
                fallbackToMotherBase(
                    state, trigger,
                    "no wall in front of Link", currentTick);
            }
            return {};
        }

        const std::uint64_t owner =
            mActors.hitOwner(result.bodyId);
        if (owner != 0
            && owner != engine::kOwnerUnresolved) {
            return abort(
                "wall is a placed actor (no camps on Zonai builds)",
                true);
        }
        if (result.normal.y > mTuning.maximumWallNormalY
            || result.normal.y < -mTuning.maximumWallNormalY) {
            return abort("wall too overhung/slabby", true);
        }
        const float horizontalNormal = squareRoot(
            result.normal.x * result.normal.x
            + result.normal.z * result.normal.z);
        if (horizontalNormal < 0.0001f) {
            return abort("degenerate wall normal", true);
        }
        state.campNx = result.normal.x / horizontalNormal;
        state.campNz = result.normal.z / horizontalNormal;
        state.wallHit = result.position;
        state.deckTopY =
            playerPosition.y - mTuning.roofTopBelowPlayer
            - mTuning.roofClearance;
        state.placeStep = runtime::PlaceStep::GROUND;
        return {};
    }

    case runtime::PlaceStep::GROUND: {
        if (!mRaycasts.castBusy()) {
            const runtime::Vec3 origin{
                state.wallHit.x + state.campNx * 2.0f,
                playerPosition.y,
                state.wallHit.z + state.campNz * 2.0f,
            };
            requestCast(
                origin,
                {origin.x,
                 state.deckTopY - mTuning.groundProbeBelowDeck,
                 origin.z},
                currentTick);
            return {};
        }
        const CastResult result = pollCast(currentTick);
        if (!result.resolved) {
            return {};
        }
        if (!result.timedOut && result.hit != 0
            && result.position.y > state.deckTopY - 0.5f) {
            state.deckTopY =
                result.position.y + mTuning.groundClearance;
        }
        state.placeStep = runtime::PlaceStep::SWEEP;
        return {};
    }

    case runtime::PlaceStep::SWEEP: {
        const int level =
            state.sweepRay / mTuning.sweepRayCount;
        const float rayY =
            level == 0
            ? state.deckTopY
            : state.deckTopY + mTuning.roofClearance;
        if (!mRaycasts.castBusy()) {
            const float along =
                mTuning.sweepOffsets[
                    state.sweepRay % mTuning.sweepRayCount];
            const float tangentX = state.campNz;
            const float tangentZ = -state.campNx;
            const runtime::Vec3 origin{
                state.wallHit.x + tangentX * along
                    + state.campNx * mTuning.sweepBackoff,
                rayY,
                state.wallHit.z + tangentZ * along
                    + state.campNz * mTuning.sweepBackoff,
            };
            requestCast(
                origin,
                {
                    origin.x
                        - state.campNx * mTuning.sweepLength,
                    rayY,
                    origin.z
                        - state.campNz * mTuning.sweepLength,
                },
                currentTick);
            return {};
        }

        const CastResult result = pollCast(currentTick);
        if (!result.resolved) {
            return {};
        }
        if (!result.timedOut && result.hit != 0) {
            const float depth =
                (result.position.x - state.wallHit.x)
                    * state.campNx
                + (result.position.z - state.wallHit.z)
                    * state.campNz;
            if (depth > mTuning.sweepMissDepth) {
                state.sweepDepth[state.sweepRay] = depth;
                const bool firstHit =
                    state.sweepHitsDeck
                        + state.sweepHitsRoof
                    == 0;
                if (firstHit || depth > state.sweepSeat) {
                    state.sweepSeat = depth;
                }
                if (level == 0) {
                    if (state.sweepHitsDeck == 0
                        || depth < state.sweepMinDeck) {
                        state.sweepMinDeck = depth;
                    }
                    ++state.sweepHitsDeck;
                } else {
                    if (state.sweepHitsRoof == 0
                        || depth < state.sweepMinRoof) {
                        state.sweepMinRoof = depth;
                    }
                    ++state.sweepHitsRoof;
                }
            }
        }

        ++state.sweepRay;
        if (state.sweepRay
            < 2 * mTuning.sweepRayCount) {
            return {};
        }
        BVLOG("sweep: deck d=(%d,%d,%d,%d,%d)cm roof d=(%d,%d,%d,%d,%d)cm",
              centimeters(state.sweepDepth[0]),
              centimeters(state.sweepDepth[1]),
              centimeters(state.sweepDepth[2]),
              centimeters(state.sweepDepth[3]),
              centimeters(state.sweepDepth[4]),
              centimeters(state.sweepDepth[5]),
              centimeters(state.sweepDepth[6]),
              centimeters(state.sweepDepth[7]),
              centimeters(state.sweepDepth[8]),
              centimeters(state.sweepDepth[9]));

        if (state.sweepHitsDeck == 0
            && state.sweepHitsRoof == 0) {
            state.sweepSeat = 0.0f;
            BVLOG("sweep: wall UNMEASURED at both levels - cantilever off the chest line");
        } else {
            if (state.sweepSeat > mTuning.maximumSeatOut) {
                BVLOG("sweep: seat clamped from %dcm to %dcm (outlier obstruction may clip)",
                      centimeters(state.sweepSeat),
                      centimeters(mTuning.maximumSeatOut));
                state.sweepSeat =
                    bivouac::placement::clampSeat(
                        state.sweepSeat,
                        mTuning.maximumSeatOut);
            }
            if (state.sweepHitsDeck > 0) {
                state.placeGapFloor =
                    bivouac::placement::boundedGap(
                        state.sweepSeat,
                        state.sweepMinDeck,
                        mTuning.maximumBackerGap);
            } else {
                BVLOG("sweep: no wall at deck level - deck cantilevers");
            }
            if (state.sweepHitsRoof > 0) {
                state.placeGapRoof =
                    bivouac::placement::boundedGap(
                        state.sweepSeat,
                        state.sweepMinRoof,
                        mTuning.maximumBackerGap);
            } else {
                BVLOG("sweep: no wall at roof level");
            }
        }

        PlacementCandidate candidate{};
        const float outwardShift =
            state.sweepSeat + mTuning.seatClearance
            + mTuning.deckHalfOut;
        candidate.anchor = {
            state.wallHit.x + state.campNx * outwardShift,
            state.deckTopY,
            state.wallHit.z + state.campNz * outwardShift,
        };
        candidate.normalX = state.campNx;
        candidate.normalZ = state.campNz;
        candidate.floorGap = state.placeGapFloor;
        candidate.roofGap = state.placeGapRoof;
        candidate.sweepSeat = state.sweepSeat;
        return commit(candidate);
    }
    }
    return {};
}

PlacementOutcome PlacementCoordinator::tick(
    runtime::PlacementState& placement,
    runtime::TriggerState& trigger,
    const runtime::CampCollection& camps,
    std::uintptr_t mainBase,
    const runtime::Vec3& playerPosition,
    const runtime::Vec3& playerForward,
    int currentTick) {
    if (bivouac::placement::timedOut(
            currentTick, trigger.trigArmedTick,
            placement.placeLane,
            mTuning.cliffTimeoutTicks,
            mTuning.motherBaseTimeoutTicks)) {
        if (placement.placeLane
            == bivouac::placement::Lane::MotherBase) {
            const bool refund =
                motherbase::shouldRefundPlacementFailure(
                    placement.motherBasePlacement.visibleWaterIntent(),
                    placement.motherBaseFallbackFromCliff);
            return abort(
                "Mother Base validation timed out", refund);
        }
        return abort("timed out (no wall read?)", true);
    }

    if (placement.placeLane
        == bivouac::placement::Lane::MotherBase) {
        return motherBaseTick(
            placement, camps, mainBase,
            playerPosition, playerForward, currentTick);
    }
    return cliffTick(
        placement, trigger, playerPosition,
        playerForward, currentTick);
}

} // namespace bivouac::feature
