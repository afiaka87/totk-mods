// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include "../CampCues.hpp"
#include "../MotherBase.hpp"
#include "../PlacementPolicy.hpp"
#include "../feature/ArrivalWatch.hpp"
#include "../feature/RefundService.hpp"

#include <cstdint>

namespace bivouac::runtime {

inline constexpr int kMaximumSites = 64;
inline constexpr int kMaximumProps = 56;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

enum class SlotState : std::uint8_t {
    UNSPAWNED = 0,
    SPAWN_REQUESTED,
    ALIVE,
    DEAD,
};

enum class PlaceStep : std::uint8_t {
    IDLE = 0,
    WALL,
    GROUND,
    SWEEP,
};

struct ActorSlot {
    SlotState state = SlotState::UNSPAWNED;
    void* preactor = nullptr;
    void* actor = nullptr;
    std::uint64_t namePtr = 0;
    bool bodySeen = false;
    bool frozenOnce = false;
    std::int8_t hardenCountdown = 0;
    int spawnedTick = 0;
    int retryAtTick = 0;
    std::uint8_t fails = 0;
};

struct Site {
    Vec3 anchor = {};
    float nx = 0.0f;
    float nz = 0.0f;
    std::uint32_t seq = 0;
    std::uint8_t tier = 0;
    float gapFloor = 0.0f;
    float gapRoof = 0.0f;
    std::uint16_t stampSlot = 0xFFFFu;
    std::uint32_t handleIndex = 0xFFFFFFFFu;
    bool waterMode = false;
    bool active = false;

    ActorSlot slot[kMaximumProps] = {};
    float distToLink = 0.0f;
};

struct SceneState {
    std::uintptr_t mainBase = 0;
    void* player = nullptr;
    int tick = 0;
    int gameplayTick = 0;
    int firstPlayerTick = 0;
    int nullPlayerTicks = 0;
};

struct TriggerState {
    volatile std::uint32_t trigPending = 0;
    std::uint8_t trigTier = 0;
    std::uint32_t trigGmdIdx = 0;
    char trigItemName[64] = {};
    int trigArmedTick = 0;
    int lastTrigTick = -1000;

    volatile int lastClimbTick = -1000;
    volatile int climbEndTick = -100000;
    bool climbFlagPrev = false;
    std::uint32_t lastHeartbeat = 0;
    int lastHeartbeatTick = -1000;
    bool heartbeatSeen = false;
};

struct MenuState {
    int menuStarvedTicks = 0;
};

struct MapState {
    bool mapServiceDirty = false;
    int serviceCursor = 0;
    int deferLogTick = -100000;
    int arrivalSite = -1;
    bivouac::feature::ArrivalWatch arrival;
    int orphanCursor = 0;
    volatile int nearStampCount = 0;
    std::uint32_t nearHandles[kMaximumSites] = {};
    int lastWidgetLogTick = -1000;
    // Camp-icon widgets seen by the icon tick, for one log line per widget per near/map change.
    static constexpr int kWidgetLogSlots = 24;
    std::uintptr_t widgetSeen[kWidgetLogSlots] = {};
    std::uint8_t widgetState[kWidgetLogSlots] = {};
    int widgetLogLines = 0;
    // Bumped whenever the stamp service writes a camp stamp; the minimap pass refreshes cells on change.
    volatile int cellEpoch = 0;
    int cellEpochSeen = -1;
    int cellMissLogs = 0;
    bool cellPassSeen = false;
};

struct AudioState {
    volatile cues::Cue pendingCue = cues::Cue::None;
    int lastPrimeTick = -1000;
};

struct PlacementState {
    placement::Lane placeLane = placement::Lane::Cliff;
    PlaceStep placeStep = PlaceStep::IDLE;
    int wallRetries = 0;
    Vec3 wallHit = {};
    float campNx = 0.0f;
    float campNz = 0.0f;
    float deckTopY = 0.0f;
    std::uint8_t sweepRay = 0;
    std::uint8_t sweepHitsDeck = 0;
    std::uint8_t sweepHitsRoof = 0;
    float sweepSeat = 0.0f;
    float sweepMinDeck = 0.0f;
    float sweepMinRoof = 0.0f;
    float sweepDepth[10] = {};
    float placeGapFloor = 0.0f;
    float placeGapRoof = 0.0f;
    motherbase::Placement motherBasePlacement;
    bool motherBaseFallbackFromCliff = false;
};

struct CampCollection {
    Site sites[kMaximumSites] = {};
    std::uint32_t nextSeq = 1;
};

struct SpawnQueueState {
    int spawnSite = -1;
    int spawnSlot = -1;
    int lastSpawnIssueTick = -1000;
    int appleDropsPending = 0;
};

struct PersistenceState {
    std::uint32_t sdState = 0;
    int sdRetryAtTick = 0;
    int sdTries = 0;
    bool ledgerLoaded = false;
    bool ledgerDirty = false;
    int ledgerWroteTick = -1000;
};

struct InputState {
    std::uint64_t prevButtons = 0;
};

struct BivouacRuntimeState {
    SceneState scene;
    TriggerState trigger;
    feature::RefundService refunds;
    MenuState menu;
    MapState map;
    AudioState audio;
    PlacementState placement;
    CampCollection camps;
    SpawnQueueState spawn;
    PersistenceState persistence;
    InputState input;
};

} // namespace bivouac::runtime
