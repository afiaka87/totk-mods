// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include "ActorRuntime.hpp"
#include "GameDataService.hpp"
#include "PlayerService.hpp"
#include "runtime/BivouacState.hpp"

#include <cstdint>

namespace bivouac::engine {

class MapStampService {
public:
    MapStampService(GameDataService& gameData,
                    ActorRuntime& actors,
                    PlayerService& players)
        : mGameData(gameData), mActors(actors), mPlayers(players) {}

    void setMainBase(std::uintptr_t mainBase) { mMainBase = mainBase; }

    void serviceCampStamps(runtime::MapState& map,
                           runtime::CampCollection& camps,
                           runtime::PersistenceState& persistence,
                           int currentTick) const;
    void clearCampStamp(runtime::Site& site,
                        runtime::PersistenceState& persistence) const;

    // Called from the map screen's A-press handler; true when a camp stamp started a warp.
    bool warpToSelectedStamp(runtime::MapState& map,
                             runtime::CampCollection& camps,
                             std::uintptr_t mapScreen,
                             std::uint32_t stampSlot) const;
    void assistArrival(runtime::MapState& map,
                       runtime::CampCollection& camps,
                       void* player,
                       const runtime::Vec3& playerPosition,
                       int currentTick) const;

private:
    bool openNavigation(MapNavigation* navigation) const;
    bool fetchSlot(const MapNavigation& navigation,
                   std::uint32_t arrayHash,
                   std::uint32_t slot,
                   void* outputHandle16) const;
    std::uint32_t readType(const MapNavigation& navigation,
                           const void* slotHandle16) const;
    bool settersReady(const MapNavigation& navigation) const;
    void logDeferred(runtime::MapState& map,
                     const MapNavigation& navigation,
                     const char* operation,
                     int currentTick) const;
    void writeStamp(const MapNavigation& navigation,
                    const void* slotHandle16,
                    float worldX,
                    float worldZ) const;
    void clearSlot(const MapNavigation& navigation,
                   const void* slotHandle16) const;
    void ensureCampStamp(runtime::Site& site,
                         runtime::PersistenceState& persistence,
                         const MapNavigation& navigation) const;
    // Clears camp icons no active site owns (ledger reset or lost record); true when the scan finished.
    bool sweepOrphanStamps(runtime::MapState& map,
                           const runtime::CampCollection& camps,
                           const MapNavigation& navigation) const;
    void* resolveWarpManager() const;
    void movePlayer(void* player, float x, float y, float z) const;

    GameDataService& mGameData;
    ActorRuntime& mActors;
    PlayerService& mPlayers;
    std::uintptr_t mMainBase = 0;
};

} // namespace bivouac::engine
