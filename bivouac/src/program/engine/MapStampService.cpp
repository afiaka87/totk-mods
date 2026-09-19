// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "MapStampService.hpp"

#include "MapPolicy.hpp"

#include <lib.hpp>

namespace bivouac::engine {
namespace {

#define BVLOG(...) Logging.Log("[bv] " __VA_ARGS__)

constexpr std::uint32_t kMapRootStructHash = 2789404124;
constexpr std::uint32_t kMapIconDataHash = 1217771808;
constexpr std::uint32_t kStampArrayHash = 3047725091;
constexpr std::uint32_t kIconTypeFieldHash = 1952810469;
constexpr std::uint32_t kStampPositionFieldHash = 2430391429;
constexpr std::uint32_t kStampLayerFieldHash = 422058490;
constexpr std::uint32_t kMapCloseForWarpFlagHash = 203206985;
constexpr std::uint32_t kMapIconNone = 2117934662;
constexpr std::uint32_t kCampStampIcon = 184548161;
constexpr std::uint32_t kLegacyCampStampIcon = 183361113;
constexpr std::uint32_t kGroundLayer = 613744949;

constexpr int kStampCapacity = 300;
constexpr std::uint16_t kNoStampSlot = 0xFFFF;
constexpr int kMaximumCampsPerTick = 2;
constexpr int kOrphanScanPerTick = 64;
constexpr int kDeferredLogGapTicks = 128;

constexpr std::ptrdiff_t kWarpToMarkerOffset = 0x0198CA5C;
constexpr std::ptrdiff_t kGetSceneComponentOffset = 0x01203C3C;
constexpr std::ptrdiff_t kSceneModuleIndirectOffset = 0x0462E180;
constexpr std::uint64_t kWarpManagerComponentIndex = 32;
constexpr std::uintptr_t kWarpTargetIndexOffset = 336;
constexpr std::uintptr_t kMapScreenWarpPendingByteOffset = 3102;

constexpr float kArrivalHoverMeters = 2.0f;
constexpr float kArrivalLiftEpsilonMeters = 0.15f;

bool validPointer(std::uintptr_t value) {
    return value >= 0x1000000 && value < 0x8000000000ull
        && (value & 0x3) == 0;
}

int centimeters(float meters) {
    return static_cast<int>(
        meters * 100.0f + (meters >= 0.0f ? 0.5f : -0.5f));
}

} // namespace

bool MapStampService::openNavigation(MapNavigation* navigation) const {
    return mGameData.openMapNavigation(
        kMapRootStructHash, kMapIconDataHash, navigation);
}

bool MapStampService::fetchSlot(
    const MapNavigation& navigation, std::uint32_t arrayHash,
    std::uint32_t slot, void* outputHandle16) const {
    return mGameData.fetchMapSlot(
        navigation, arrayHash, slot, outputHandle16);
}

std::uint32_t MapStampService::readType(
    const MapNavigation& navigation, const void* slotHandle16) const {
    std::uint32_t type = 0;
    return mGameData.readEnum(
               navigation, slotHandle16, kIconTypeFieldHash, &type)
        ? type
        : 0;
}

bool MapStampService::settersReady(
    const MapNavigation& navigation) const {
    return mGameData.mapSettersReady(navigation, 2, 1);
}

void MapStampService::logDeferred(
    runtime::MapState& map, const MapNavigation& navigation,
    const char* operation, int currentTick) const {
    if (currentTick - map.deferLogTick < kDeferredLogGapTicks) {
        return;
    }
    map.deferLogTick = currentTick;
    const QueueSnapshot queues = mGameData.mapQueueSnapshot(navigation);
    BVLOG("map: %s deferred (enum q cap=%d idx=%u, vec2 q cap=%d idx=%u)",
          operation, queues.enumCapacity, queues.enumWriteIndex,
          queues.vector2Capacity, queues.vector2WriteIndex);
}

void MapStampService::writeStamp(
    const MapNavigation& navigation, const void* slotHandle16,
    float worldX, float worldZ) const {
    mGameData.writeEnum(
        navigation, kCampStampIcon, slotHandle16, kIconTypeFieldHash);
    const float position[2] = {worldX, worldZ};
    mGameData.writeVector2(
        navigation, position, slotHandle16, kStampPositionFieldHash);
    mGameData.writeEnum(
        navigation, kGroundLayer, slotHandle16, kStampLayerFieldHash);
}

void MapStampService::clearSlot(
    const MapNavigation& navigation, const void* slotHandle16) const {
    mGameData.writeEnum(
        navigation, kMapIconNone, slotHandle16, kIconTypeFieldHash);
}

// The widget compares its bound handle index against this value; both come from the same getter.
void noteHandleIndex(runtime::Site& site, const unsigned char* handle) {
    const std::uint32_t index = *reinterpret_cast<const std::uint32_t*>(handle + 8);
    if (site.handleIndex != index) {
        site.handleIndex = index;
        BVLOG("map: camp #%u stamp slot %u handle index %u", site.seq, site.stampSlot, index);
    }
}

void MapStampService::ensureCampStamp(
    runtime::Site& site, runtime::PersistenceState& persistence,
    const MapNavigation& navigation) const {
    alignas(8) unsigned char handle[16];
    if (site.stampSlot != kNoStampSlot
        && site.stampSlot < kStampCapacity) {
        if (fetchSlot(
                navigation, kStampArrayHash, site.stampSlot, handle)) {
            noteHandleIndex(site, handle);
            const std::uint32_t type = readType(navigation, handle);
            if (bivouac::map::ownsStampType(
                    type, kCampStampIcon, kLegacyCampStampIcon)
                || type == kMapIconNone) {
                writeStamp(
                    navigation, handle, site.anchor.x, site.anchor.z);
                return;
            }
        }
        site.stampSlot = kNoStampSlot;
    site.handleIndex = 0xFFFFFFFFu;
    }

    for (int slot = 0; slot < kStampCapacity; ++slot) {
        if (!fetchSlot(
                navigation, kStampArrayHash,
                static_cast<std::uint32_t>(slot), handle)) {
            continue;
        }
        if (readType(navigation, handle) != kMapIconNone) {
            continue;
        }
        writeStamp(navigation, handle, site.anchor.x, site.anchor.z);
        site.stampSlot = static_cast<std::uint16_t>(slot);
        noteHandleIndex(site, handle);
        persistence.ledgerDirty = true;
        return;
    }
    BVLOG("map: no free stamp slot for camp #%u (map full?)", site.seq);
}

void MapStampService::serviceCampStamps(
    runtime::MapState& map, runtime::CampCollection& camps,
    runtime::PersistenceState& persistence, int currentTick) const {
    MapNavigation navigation;
    if (!openNavigation(&navigation)) {
        return;
    }

    int served = 0;
    for (int offset = 0; offset < runtime::kMaximumSites; ++offset) {
        const int siteIndex =
            (map.serviceCursor + offset) % runtime::kMaximumSites;
        runtime::Site& site = camps.sites[siteIndex];
        if (!site.active) {
            continue;
        }
        if (served >= kMaximumCampsPerTick) {
            map.serviceCursor = siteIndex;
            map.mapServiceDirty = true;
            return;
        }
        if (!settersReady(navigation)) {
            logDeferred(map, navigation, "stamp service", currentTick);
            map.serviceCursor = siteIndex;
            map.mapServiceDirty = true;
            return;
        }
        ensureCampStamp(site, persistence, navigation);
        map.cellEpoch = map.cellEpoch + 1;
        ++served;
    }
    map.serviceCursor = 0;
    map.mapServiceDirty = persistence.ledgerLoaded
        && !sweepOrphanStamps(map, camps, navigation);
}

bool MapStampService::sweepOrphanStamps(
    runtime::MapState& map, const runtime::CampCollection& camps,
    const MapNavigation& navigation) const {
    alignas(8) unsigned char handle[16];
    int cleared = 0;
    int scanned = 0;
    while (map.orphanCursor < kStampCapacity) {
        if (scanned++ >= kOrphanScanPerTick) {
            return false;
        }
        const std::uint32_t slot = static_cast<std::uint32_t>(map.orphanCursor);
        if (!fetchSlot(navigation, kStampArrayHash, slot, handle)) {
            ++map.orphanCursor;
            continue;
        }
        const std::uint32_t type = readType(navigation, handle);
        if (!bivouac::map::ownsStampType(type, kCampStampIcon, kLegacyCampStampIcon)
            || bivouac::map::siteOwningStampSlot(camps.sites, runtime::kMaximumSites, slot) >= 0) {
            ++map.orphanCursor;
            continue;
        }
        if (cleared >= kMaximumCampsPerTick || !settersReady(navigation)) {
            return false;
        }
        clearSlot(navigation, handle);
        BVLOG("map: cleared orphan camp icon in stamp slot %u", slot);
        ++cleared;
        ++map.orphanCursor;
    }
    map.orphanCursor = 0;
    return true;
}

void MapStampService::clearCampStamp(
    runtime::Site& site,
    runtime::PersistenceState& persistence) const {
    if (site.stampSlot != kNoStampSlot) {
        MapNavigation navigation;
        if (openNavigation(&navigation) && settersReady(navigation)) {
            alignas(8) unsigned char handle[16];
            if (fetchSlot(
                    navigation, kStampArrayHash, site.stampSlot, handle)) {
                const std::uint32_t type = readType(navigation, handle);
                if (bivouac::map::ownsStampType(
                        type, kCampStampIcon, kLegacyCampStampIcon)) {
                    clearSlot(navigation, handle);
                }
            }
        } else {
            BVLOG("map: teardown of camp #%u could not clear its stamp (gmd unready)",
                  site.seq);
        }
        persistence.ledgerDirty = true;
    }
    site.stampSlot = kNoStampSlot;
    site.handleIndex = 0xFFFFFFFFu;
}

void* MapStampService::resolveWarpManager() const {
    const auto holder =
        *reinterpret_cast<std::uintptr_t*>(
            mMainBase + kSceneModuleIndirectOffset);
    if (!validPointer(holder)) {
        return nullptr;
    }
    const auto sceneModule =
        *reinterpret_cast<std::uintptr_t*>(holder);
    if (!validPointer(sceneModule)) {
        return nullptr;
    }
    const auto getComponent =
        reinterpret_cast<void* (*)(void*, std::uint64_t)>(
            mMainBase + kGetSceneComponentOffset);
    void* manager = getComponent(
        reinterpret_cast<void*>(sceneModule),
        kWarpManagerComponentIndex);
    return validPointer(reinterpret_cast<std::uintptr_t>(manager))
        ? manager
        : nullptr;
}

bool MapStampService::warpToSelectedStamp(
    runtime::MapState& map, runtime::CampCollection& camps,
    std::uintptr_t mapScreen, std::uint32_t stampSlot) const {
    if (!validPointer(mapScreen) || stampSlot >= kStampCapacity) {
        return false;
    }
    MapNavigation navigation;
    if (!openNavigation(&navigation)) {
        BVLOG("map: stamp %u selected but GameData unavailable", stampSlot);
        return false;
    }
    alignas(8) unsigned char handle[16];
    if (!fetchSlot(navigation, kStampArrayHash, stampSlot, handle)) {
        return false;
    }
    const std::uint32_t type = readType(navigation, handle);
    if (!bivouac::map::ownsStampType(
            type, kCampStampIcon, kLegacyCampStampIcon)) {
        return false;
    }
    const int siteIndex = bivouac::map::siteOwningStampSlot(
        camps.sites, runtime::kMaximumSites, stampSlot);
    if (siteIndex < 0) {
        BVLOG("map: camp stamp %u has no owning camp - vanilla edit menu", stampSlot);
        return false;
    }
    void* warpManager = resolveWarpManager();
    if (warpManager == nullptr) {
        BVLOG("map: camp #%u selected but WarpMgr unresolved",
              camps.sites[siteIndex].seq);
        return false;
    }

    runtime::Site& site = camps.sites[siteIndex];
    const float position[3] = {
        site.anchor.x,
        site.anchor.y + kArrivalHoverMeters,
        site.anchor.z,
    };
    const float rotation[3] = {};
    *reinterpret_cast<std::int32_t*>(
        reinterpret_cast<std::uintptr_t>(warpManager)
        + kWarpTargetIndexOffset) = -1;
    const auto warp =
        reinterpret_cast<void (*)(void*, const void*, const void*)>(
            mMainBase + kWarpToMarkerOffset);
    warp(warpManager, position, rotation);
    // Same close-out the map's own travel confirm performs after warpToWarpMarker.
    mGameData.writeBool(
        navigation, true, navigation.root, kMapCloseForWarpFlagHash);
    *reinterpret_cast<std::uint8_t*>(
        mapScreen + kMapScreenWarpPendingByteOffset) = 0;
    map.arrivalSite = siteIndex;
    map.arrival = feature::ArrivalWatch{}; // armed on the next game tick
    BVLOG("map: WARP to camp #%u (stamp slot %u) at (%d,%d,%d)cm",
          site.seq, stampSlot, centimeters(site.anchor.x),
          centimeters(site.anchor.y), centimeters(site.anchor.z));
    return true;
}

void MapStampService::movePlayer(
    void* player, float x, float y, float z) const {
    if (player == nullptr) {
        return;
    }
    float rotation[9] = {};
    mPlayers.copyRotation(player, rotation);
    const float matrix[12] = {
        rotation[0], rotation[1], rotation[2], x,
        rotation[3], rotation[4], rotation[5], y,
        rotation[6], rotation[7], rotation[8], z,
    };
    mActors.forceSetMatrix(player, matrix);
}

void MapStampService::assistArrival(
    runtime::MapState& map, runtime::CampCollection& camps,
    void* player, const runtime::Vec3& playerPosition,
    int currentTick) const {
    if (map.arrivalSite < 0) {
        return;
    }
    if (map.arrivalSite >= runtime::kMaximumSites
        || !camps.sites[map.arrivalSite].active) {
        BVLOG("map: arrival watch dropped - camp slot %d no longer active", map.arrivalSite);
        map.arrivalSite = -1;
        return;
    }

    if (map.arrival.phase == feature::ArrivalPhase::Idle) {
        feature::armArrival(map.arrival, currentTick);
    }
    runtime::Site& site = camps.sites[map.arrivalSite];
    const runtime::ActorSlot& deck = site.slot[0];
    const bool deckReady = deck.state == runtime::SlotState::ALIVE && deck.frozenOnce;
    const feature::ArrivalInput input{
        currentTick, playerPosition.x, playerPosition.y, playerPosition.z,
        site.anchor.x, site.anchor.y, site.anchor.z, deckReady};
    const int linkAboveDeck = centimeters(playerPosition.y - site.anchor.y);
    switch (feature::stepArrival(map.arrival, input)) {
    case feature::ArrivalAction::Arrived:
        BVLOG("map: arrived at camp #%u %d ticks after the warp, Link %dcm above the deck, deck %s",
              site.seq, currentTick - map.arrival.armedTick, linkAboveDeck,
              deckReady ? "ready" : "not spawned yet");
        break;
    case feature::ArrivalAction::DeckReady:
        BVLOG("map: camp #%u deck ready %d ticks after arrival, Link %dcm above it",
              site.seq, currentTick - map.arrival.arrivedTick, linkAboveDeck);
        break;
    case feature::ArrivalAction::Lift:
        movePlayer(
            player, site.anchor.x,
            site.anchor.y + kArrivalLiftEpsilonMeters,
            site.anchor.z);
        BVLOG("map: arrival lift %d - Link was %dcm under camp #%u's deck, %d ticks after arrival",
              map.arrival.lifts, -linkAboveDeck, site.seq,
              currentTick - map.arrival.arrivedTick);
        break;
    case feature::ArrivalAction::EndedSettled:
        BVLOG("map: arrival watch ended at camp #%u - Link %dcm above the deck, %d lift(s)",
              site.seq, linkAboveDeck, map.arrival.lifts);
        map.arrivalSite = -1;
        break;
    case feature::ArrivalAction::EndedDeckNeverReady:
        BVLOG("map: arrival watch ended at camp #%u - deck never spawned (state %d), Link %dcm above its spot",
              site.seq, static_cast<int>(deck.state), linkAboveDeck);
        map.arrivalSite = -1;
        break;
    case feature::ArrivalAction::EndedNoArrival:
        BVLOG("map: arrival watch ended - Link never reached camp #%u (%dcm above its spot)",
              site.seq, linkAboveDeck);
        map.arrivalSite = -1;
        break;
    case feature::ArrivalAction::None:
        break;
    }
}

} // namespace bivouac::engine
