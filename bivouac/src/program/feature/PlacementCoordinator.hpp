// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include "engine/ActorRuntime.hpp"
#include "engine/RaycastService.hpp"
#include "engine/TerrainWaterService.hpp"
#include "runtime/BivouacState.hpp"

#include <cstdint>

namespace bivouac::feature {

struct PlacementTuning {
    int cliffTimeoutTicks = 0;
    int motherBaseTimeoutTicks = 0;
    std::uint32_t collisionMask = 0;
    int castTimeoutTicks = 0;

    float wallCastBackoff = 0.0f;
    float wallCastLength = 0.0f;
    int wallCastRetries = 0;
    float maximumWallNormalY = 0.0f;
    float roofClearance = 0.0f;
    float roofTopBelowPlayer = 0.0f;
    float groundProbeBelowDeck = 0.0f;
    float groundClearance = 0.0f;

    float sweepOffsets[5] = {};
    int sweepRayCount = 0;
    float sweepBackoff = 0.0f;
    float sweepLength = 0.0f;
    float sweepMissDepth = 0.0f;
    float sweepDiagnosticMiss = 0.0f;
    float seatClearance = 0.0f;
    float maximumSeatOut = 0.0f;
    float maximumBackerGap = 0.0f;
    float deckHalfOut = 0.0f;

    std::uint32_t motherBaseWaterQueriesPerTick = 0;
    float motherBaseSiteSeparation = 0.0f;
};

struct PlacementCandidate {
    runtime::Vec3 anchor{};
    float normalX = 0.0f;
    float normalZ = 0.0f;
    float floorGap = 0.0f;
    float roofGap = 0.0f;
    bool waterMode = false;

    float sweepSeat = 0.0f;
    float anchorDistance = 0.0f;
    float minimumWaterDepth = 0.0f;
    float maximumWaterDepth = 0.0f;
    std::uint16_t waterSamples = 0;
    std::uint16_t clearanceSamples = 0;
    std::uint8_t candidateAttempts = 0;
};

enum class PlacementOutcomeKind : std::uint8_t {
    None = 0,
    Commit,
    Abort,
};

struct PlacementOutcome {
    PlacementOutcomeKind kind = PlacementOutcomeKind::None;
    const char* reason = nullptr;
    bool refund = false;
    PlacementCandidate candidate{};
};

class PlacementCoordinator {
public:
    PlacementCoordinator(engine::RaycastService& raycasts,
                         engine::ActorRuntime& actors)
        : mRaycasts(raycasts), mActors(actors) {}

    void configure(const PlacementTuning& tuning) {
        mTuning = tuning;
    }

    PlacementOutcome tick(
        runtime::PlacementState& placement,
        runtime::TriggerState& trigger,
        const runtime::CampCollection& camps,
        std::uintptr_t mainBase,
        const runtime::Vec3& playerPosition,
        const runtime::Vec3& playerForward,
        int currentTick);

    void reset(runtime::PlacementState& placement);

private:
    struct CastResult {
        bool resolved = false;
        bool timedOut = false;
        std::uint32_t hit = 0;
        float distance = 0.0f;
        runtime::Vec3 position{};
        runtime::Vec3 normal{};
        std::uint32_t bodyId = 0;
    };

    void requestCast(const runtime::Vec3& from,
                     const runtime::Vec3& to,
                     int currentTick);
    CastResult pollCast(int currentTick);
    PlacementOutcome cliffTick(
        runtime::PlacementState& placement,
        runtime::TriggerState& trigger,
        const runtime::Vec3& playerPosition,
        const runtime::Vec3& playerForward,
        int currentTick);
    PlacementOutcome motherBaseTick(
        runtime::PlacementState& placement,
        const runtime::CampCollection& camps,
        std::uintptr_t mainBase,
        const runtime::Vec3& playerPosition,
        const runtime::Vec3& playerForward,
        int currentTick);
    void fallbackToMotherBase(
        runtime::PlacementState& placement,
        runtime::TriggerState& trigger,
        const char* reason,
        int currentTick);
    int nearestSite(
        const runtime::CampCollection& camps,
        const runtime::Vec3& position,
        float* distance) const;

    engine::RaycastService& mRaycasts;
    engine::ActorRuntime& mActors;
    engine::TerrainWaterService mTerrainWater;
    PlacementTuning mTuning{};
};

} // namespace bivouac::feature
