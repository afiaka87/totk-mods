// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include "CampGeometry.hpp"
#include "engine/ActorRuntime.hpp"
#include "runtime/BivouacState.hpp"

namespace bivouac::feature {

struct CampAssemblerTuning {
    BackerTuning backers{};
    float minimumBackerShift = 0.0f;
    float wantedSiteRadius = 0.0f;
    int maximumLiveSites = 0;
    int spawnGapTicks = 0;
    int spawnTimeoutTicks = 0;
    int retryTicks = 0;
    int maximumFailures = 0;
    int diedYoungTicks = 0;
    int hardenDelayTicks = 0;
    int schedulerPeriodTicks = 0;
    const char* provisioningActorName = nullptr;
    int provisioningCount = 0;
};

class CampAssembler {
public:
    explicit CampAssembler(engine::ActorRuntime& actors)
        : mActors(actors) {}

    void configure(const PropDefinition* props,
                   int propCount,
                   const CampAssemblerTuning& tuning);

    void resetRuntime(runtime::CampCollection& camps,
                      runtime::SpawnQueueState& spawn) const;
    void teardownSite(runtime::Site& site) const;

    void pollSpawn(runtime::CampCollection& camps,
                   runtime::SpawnQueueState& spawn,
                   int currentTick) const;
    void maintainActors(runtime::CampCollection& camps,
                        int currentTick) const;
    void schedule(runtime::CampCollection& camps,
                  runtime::SpawnQueueState& spawn,
                  const runtime::Vec3& playerPosition,
                  int currentTick) const;
    void provision(runtime::SpawnQueueState& spawn,
                   const runtime::Vec3& playerPosition,
                   int currentTick) const;

private:
    float backerShift(const runtime::Site& site,
                      const PropDefinition& prop) const;
    PropTransform propTransform(const runtime::Site& site,
                                int propIndex) const;
    void requestPropSpawn(runtime::CampCollection& camps,
                          runtime::SpawnQueueState& spawn,
                          int siteIndex,
                          int propIndex,
                          int currentTick) const;
    void freezeAtTransform(void* actor,
                           void* body,
                           const PropTransform& transform) const;
    void slotDied(runtime::ActorSlot& slot,
                  bool diedYoung,
                  int currentTick) const;
    void resetSiteRuntime(runtime::Site& site,
                          const char* reason) const;

    engine::ActorRuntime& mActors;
    const PropDefinition* mProps = nullptr;
    int mPropCount = 0;
    CampAssemblerTuning mTuning{};
};

} // namespace bivouac::feature
