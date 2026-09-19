// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "CampAssembler.hpp"

#include "CampAssemblyPolicy.hpp"

#include <lib.hpp>

namespace bivouac::feature {
namespace {

#define BVLOG(...) Logging.Log("[bv] " __VA_ARGS__)

constexpr std::uint32_t kMotionFixedStatic = 0;
constexpr std::uint32_t kMotionKeyframed = 1;
constexpr std::uint32_t kMotionDynamic = 2;
constexpr std::uint8_t kNoDependency = 0xFF;

bool namesMatch(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    for (int index = 0; index < 64; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
        if (left[index] == '\0') {
            return true;
        }
    }
    return false;
}

float squareRoot(float value) {
    return value > 0.0f ? __builtin_sqrtf(value) : 0.0f;
}

} // namespace

void CampAssembler::configure(
    const PropDefinition* props, int propCount,
    const CampAssemblerTuning& tuning) {
    mProps = props;
    mPropCount = propCount;
    mTuning = tuning;
}

float CampAssembler::backerShift(
    const runtime::Site& site, const PropDefinition& prop) const {
    const SiteGeometry geometry{
        {site.anchor.x, site.anchor.y, site.anchor.z},
        site.nx, site.nz, site.gapFloor, site.gapRoof,
    };
    return bivouac::feature::backerShift(
        geometry, prop, mTuning.backers);
}

PropTransform CampAssembler::propTransform(
    const runtime::Site& site, int propIndex) const {
    const SiteGeometry geometry{
        {site.anchor.x, site.anchor.y, site.anchor.z},
        site.nx, site.nz, site.gapFloor, site.gapRoof,
    };
    return worldTransform(
        geometry, mProps[propIndex], mTuning.backers);
}

void CampAssembler::freezeAtTransform(
    void* actor, void* body, const PropTransform& transform) const {
    mActors.requestMotionType(body, kMotionDynamic);
    const float matrix[12] = {
        transform.column0.x, transform.column1.x,
        transform.column2.x, transform.position.x,
        transform.column0.y, transform.column1.y,
        transform.column2.y, transform.position.y,
        transform.column0.z, transform.column1.z,
        transform.column2.z, transform.position.z,
    };
    mActors.forceSetMatrix(actor, matrix);
    mActors.zeroLinearVelocity(body);
    mActors.requestMotionType(body, kMotionKeyframed);
}

void CampAssembler::requestPropSpawn(
    runtime::CampCollection& camps, runtime::SpawnQueueState& spawn,
    int siteIndex, int propIndex, int currentTick) const {
    runtime::Site& site = camps.sites[siteIndex];
    runtime::ActorSlot& slot = site.slot[propIndex];
    const PropDefinition& prop = mProps[propIndex];
    const PropTransform finalTransform =
        propTransform(site, propIndex);

    engine::SpawnTransform spawnTransform{};
    spawnTransform.position[0] = finalTransform.position.x;
    spawnTransform.position[1] = finalTransform.position.y;
    spawnTransform.position[2] = finalTransform.position.z;
    spawnTransform.rotation[0] = finalTransform.column0.x;
    spawnTransform.rotation[1] = finalTransform.column1.x;
    spawnTransform.rotation[2] = finalTransform.column2.x;
    spawnTransform.rotation[3] = finalTransform.column0.y;
    spawnTransform.rotation[4] = finalTransform.column1.y;
    spawnTransform.rotation[5] = finalTransform.column2.y;
    spawnTransform.rotation[6] = finalTransform.column0.z;
    spawnTransform.rotation[7] = finalTransform.column1.z;
    spawnTransform.rotation[8] = finalTransform.column2.z;

    const engine::SpawnResult result =
        mActors.requestSpawn(prop.actorName, spawnTransform);
    spawn.lastSpawnIssueTick = currentTick;
    if (result.preactor != nullptr) {
        slot.state = runtime::SlotState::SPAWN_REQUESTED;
        slot.preactor = result.preactor;
        slot.spawnedTick = currentTick;
        spawn.spawnSite = siteIndex;
        spawn.spawnSlot = propIndex;
        BVLOG("site#%u slot%d(%s): spawn requested (ret=%d code=%u)",
              site.seq, propIndex, prop.actorName,
              result.accepted ? 1 : 0, result.nativeResult);
        return;
    }

    ++slot.fails;
    slot.retryAtTick = retryAtTick(
        currentTick, mTuning.retryTicks, slot.fails,
        static_cast<std::uint8_t>(mTuning.maximumFailures));
    BVLOG("site#%u slot%d(%s): spawn FAILED (ret=%d code=%u, fail #%u)",
          site.seq, propIndex, prop.actorName,
          result.accepted ? 1 : 0, result.nativeResult, slot.fails);
}

void CampAssembler::provision(
    runtime::SpawnQueueState& spawn,
    const runtime::Vec3& playerPosition,
    int currentTick) const {
    if (spawn.appleDropsPending <= 0
        || spawn.spawnSite >= 0
        || currentTick - spawn.lastSpawnIssueTick
            < mTuning.spawnGapTicks
        || mTuning.provisioningActorName == nullptr
        || mTuning.provisioningCount != 10) {
        return;
    }

    static constexpr float kRing[10][2] = {
        {1.000f, 0.000f}, {0.809f, 0.588f}, {0.309f, 0.951f},
        {-0.309f, 0.951f}, {-0.809f, 0.588f}, {-1.000f, 0.000f},
        {-0.809f, -0.588f}, {-0.309f, -0.951f},
        {0.309f, -0.951f}, {0.809f, -0.588f},
    };
    constexpr float kRingRadius = 0.9f;
    constexpr float kDropLift = 1.0f;
    const int index =
        mTuning.provisioningCount - spawn.appleDropsPending;

    engine::SpawnTransform transform{};
    transform.position[0] =
        playerPosition.x + kRing[index][0] * kRingRadius;
    transform.position[1] = playerPosition.y + kDropLift;
    transform.position[2] =
        playerPosition.z + kRing[index][1] * kRingRadius;
    transform.rotation[0] = 1.0f;
    transform.rotation[4] = 1.0f;
    transform.rotation[8] = 1.0f;
    const engine::SpawnResult result =
        mActors.requestSpawn(
            mTuning.provisioningActorName, transform);

    spawn.lastSpawnIssueTick = currentTick;
    --spawn.appleDropsPending;
    BVLOG("apple drop %d/%d: ret=%d code=%u pre=%d",
          index + 1, mTuning.provisioningCount,
          result.accepted ? 1 : 0, result.nativeResult,
          result.preactor != nullptr ? 1 : 0);
}

void CampAssembler::resetRuntime(
    runtime::CampCollection& camps,
    runtime::SpawnQueueState& spawn) const {
    for (runtime::Site& site : camps.sites) {
        if (!site.active) {
            continue;
        }
        for (int propIndex = 0; propIndex < mPropCount; ++propIndex) {
            site.slot[propIndex] = {};
        }
    }
    spawn.spawnSite = -1;
    spawn.spawnSlot = -1;
}

void CampAssembler::teardownSite(runtime::Site& site) const {
    for (int propIndex = 0; propIndex < mPropCount; ++propIndex) {
        runtime::ActorSlot& slot = site.slot[propIndex];
        if (slot.state == runtime::SlotState::ALIVE) {
            mActors.requestDelete(slot.actor);
        }
        slot = {};
    }
}

void CampAssembler::slotDied(
    runtime::ActorSlot& slot, bool diedYoung,
    int currentTick) const {
    const std::uint8_t failures =
        diedYoung
        ? static_cast<std::uint8_t>(slot.fails + 1)
        : slot.fails;
    slot = {};
    slot.state = runtime::SlotState::DEAD;
    slot.fails = failures;
    slot.retryAtTick = retryAtTick(
        currentTick, mTuning.retryTicks, failures,
        static_cast<std::uint8_t>(mTuning.maximumFailures));
}

void CampAssembler::resetSiteRuntime(
    runtime::Site& site, const char* reason) const {
    for (int propIndex = 0; propIndex < mPropCount; ++propIndex) {
        runtime::ActorSlot& slot = site.slot[propIndex];
        if (slot.state == runtime::SlotState::ALIVE
            || slot.state == runtime::SlotState::SPAWN_REQUESTED) {
            const std::uint8_t failures = slot.fails;
            const int retryAtTick = slot.retryAtTick;
            slot = {};
            slot.fails = failures;
            slot.retryAtTick = retryAtTick;
        }
    }
    BVLOG("site#%u: runtime reset (%s) - will lazily respawn",
          site.seq, reason);
}

void CampAssembler::pollSpawn(
    runtime::CampCollection& camps,
    runtime::SpawnQueueState& spawn,
    int currentTick) const {
    if (spawn.spawnSite < 0) {
        return;
    }
    runtime::Site& site = camps.sites[spawn.spawnSite];
    runtime::ActorSlot& slot = site.slot[spawn.spawnSlot];
    if (slot.state != runtime::SlotState::SPAWN_REQUESTED) {
        spawn.spawnSite = -1;
        spawn.spawnSlot = -1;
        return;
    }

    void* actor = mActors.actorFromPreactor(slot.preactor);
    if (actor != nullptr) {
        const char* name = mActors.actorName(actor);
        if (!namesMatch(name, mProps[spawn.spawnSlot].actorName)) {
            BVLOG("site#%u slot%d: preactor resolved to a DIFFERENT actor (got '%s', wanted '%s') - retrying",
                  site.seq, spawn.spawnSlot,
                  name != nullptr ? name : "<null>",
                  mProps[spawn.spawnSlot].actorName);
            slot = {};
            ++slot.fails;
            slot.retryAtTick = currentTick + mTuning.retryTicks;
        } else {
            slot.actor = actor;
            slot.namePtr = reinterpret_cast<std::uint64_t>(
                mActors.actorName(actor));
            slot.preactor = nullptr;
            BVLOG("site#%u slot%d(%s): ALIVE%s",
                  site.seq, spawn.spawnSlot,
                  mProps[spawn.spawnSlot].actorName,
                  mProps[spawn.spawnSlot].backerLevel
                      ? " (backer slab)" : "");
            slot.state = runtime::SlotState::ALIVE;
        }
        spawn.spawnSite = -1;
        spawn.spawnSlot = -1;
        return;
    }

    if (currentTick - slot.spawnedTick
        <= mTuning.spawnTimeoutTicks) {
        return;
    }
    BVLOG("site#%u slot%d(%s): spawn timeout",
          site.seq, spawn.spawnSlot,
          mProps[spawn.spawnSlot].actorName);
    const std::uint8_t failures = slot.fails;
    slot = {};
    slot.fails = static_cast<std::uint8_t>(failures + 1);
    slot.retryAtTick = retryAtTick(
        currentTick, mTuning.retryTicks, slot.fails,
        static_cast<std::uint8_t>(mTuning.maximumFailures));
    spawn.spawnSite = -1;
    spawn.spawnSlot = -1;
}

void CampAssembler::maintainActors(
    runtime::CampCollection& camps, int currentTick) const {
    for (runtime::Site& site : camps.sites) {
        if (!site.active) {
            continue;
        }
        for (int propIndex = 0; propIndex < mPropCount; ++propIndex) {
            runtime::ActorSlot& slot = site.slot[propIndex];
            if (slot.state != runtime::SlotState::ALIVE) {
                continue;
            }
            const PropDefinition& prop = mProps[propIndex];

            if (reinterpret_cast<std::uint64_t>(
                    mActors.actorName(slot.actor))
                != slot.namePtr) {
                const int livedTicks =
                    currentTick - slot.spawnedTick;
                BVLOG("site#%u slot%d: actor memory reused - DEAD (lived %d ticks)",
                      site.seq, propIndex, livedTicks);
                slotDied(
                    slot, livedTicks < mTuning.diedYoungTicks,
                    currentTick);
                if (propIndex == 0) {
                    resetSiteRuntime(site, "deck memory reused");
                }
                continue;
            }
            if (!prop.expectBody) {
                continue;
            }

            void* body = mActors.mainRigidBody(slot.actor);
            if (body == nullptr) {
                if (slot.bodySeen) {
                    const int livedTicks =
                        currentTick - slot.spawnedTick;
                    const bool diedYoung =
                        livedTicks < mTuning.diedYoungTicks;
                    BVLOG("site#%u slot%d(%s): rigid body GONE - DEAD (lived %d ticks%s)",
                          site.seq, propIndex, prop.actorName,
                          livedTicks,
                          diedYoung
                              ? " - died young, backing off" : "");
                    slotDied(
                        slot, diedYoung, currentTick);
                    if (propIndex == 0) {
                        resetSiteRuntime(
                            site, "deck reaped (site sentinel)");
                    }
                }
                continue;
            }
            if (!slot.bodySeen) {
                slot.bodySeen = true;
            }

            if (prop.freeze && !slot.frozenOnce) {
                const PropTransform transform =
                    propTransform(site, propIndex);
                freezeAtTransform(slot.actor, body, transform);
                slot.frozenOnce = true;
                slot.hardenCountdown =
                    static_cast<std::int8_t>(
                        mTuning.hardenDelayTicks);
                BVLOG("site#%u slot%d(%s): frozen at pose",
                      site.seq, propIndex, prop.actorName);
            }
            if (slot.hardenCountdown > 0
                && --slot.hardenCountdown == 0) {
                mActors.requestMotionType(body, kMotionFixedStatic);
            }
        }
    }
}

void CampAssembler::schedule(
    runtime::CampCollection& camps,
    runtime::SpawnQueueState& spawn,
    const runtime::Vec3& playerPosition,
    int currentTick) const {
    if (currentTick % mTuning.schedulerPeriodTicks == 0) {
        for (runtime::Site& site : camps.sites) {
            if (!site.active) {
                continue;
            }
            const float dx = site.anchor.x - playerPosition.x;
            const float dy = site.anchor.y - playerPosition.y;
            const float dz = site.anchor.z - playerPosition.z;
            site.distToLink =
                squareRoot(dx * dx + dy * dy + dz * dz);
        }
    }

    int wanted[runtime::kMaximumSites] = {};
    int wantedCount = 0;
    for (int siteIndex = 0;
         siteIndex < runtime::kMaximumSites; ++siteIndex) {
        const runtime::Site& site = camps.sites[siteIndex];
        if (!site.active
            || site.distToLink > mTuning.wantedSiteRadius) {
            continue;
        }
        int insertion =
            wantedCount < mTuning.maximumLiveSites
            ? wantedCount
            : -1;
        for (int index = 0; index < wantedCount; ++index) {
            if (site.distToLink
                < camps.sites[wanted[index]].distToLink) {
                insertion = index;
                break;
            }
        }
        if (insertion < 0) {
            continue;
        }
        const int finalIndex =
            wantedCount < mTuning.maximumLiveSites
            ? wantedCount
            : mTuning.maximumLiveSites - 1;
        for (int index = finalIndex;
             index > insertion; --index) {
            wanted[index] = wanted[index - 1];
        }
        wanted[insertion] = siteIndex;
        if (wantedCount < mTuning.maximumLiveSites) {
            ++wantedCount;
        }
    }

    if (spawn.spawnSite >= 0
        || currentTick - spawn.lastSpawnIssueTick
            < mTuning.spawnGapTicks) {
        return;
    }
    for (int wantedIndex = 0;
         wantedIndex < wantedCount; ++wantedIndex) {
        const int siteIndex = wanted[wantedIndex];
        runtime::Site& site = camps.sites[siteIndex];
        for (int propIndex = 0;
             propIndex < mPropCount; ++propIndex) {
            runtime::ActorSlot& slot = site.slot[propIndex];
            const PropDefinition& prop = mProps[propIndex];
            const bool dependencyAlive =
                prop.dependsOn == kNoDependency
                || site.slot[prop.dependsOn].state
                    == runtime::SlotState::ALIVE;
            const bool sentinelReady =
                site.slot[0].state == runtime::SlotState::ALIVE
                && site.slot[0].frozenOnce;
            const SpawnFacts facts{
                prop.tierMask,
                site.tier,
                site.waterMode,
                prop.backerLevel,
                prop.backerLevel != 0
                    ? backerShift(site, prop)
                    : 0.0f,
                mTuning.minimumBackerShift,
                prop.dependsOn != kNoDependency,
                dependencyAlive,
                static_cast<AssemblySlotState>(slot.state),
                slot.fails,
                static_cast<std::uint8_t>(
                    mTuning.maximumFailures),
                currentTick,
                slot.retryAtTick,
                site.distToLink,
                prop.spawnRadius,
                propIndex == 0,
                sentinelReady,
            };
            if (!shouldSpawn(facts)) {
                if (propIndex != 0 && !sentinelReady) {
                    break;
                }
                continue;
            }
            requestPropSpawn(
                camps, spawn, siteIndex, propIndex, currentTick);
            return;
        }
    }
}

} // namespace bivouac::feature
