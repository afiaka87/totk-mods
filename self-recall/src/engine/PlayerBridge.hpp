#pragma once

#include <cstdint>

#include "RecallHistory.hpp"
#include "RecallOutfitSpeed.hpp"
#include "totk/core/Units.hpp"

namespace self_recall::world {

using InvalidateFn = void (*)(const char* reason);

struct PlayerBridgeState {
    std::uintptr_t mainBase = 0;
    totk::core::SceneToken sceneToken{};
    std::uintptr_t playerAddress = 0;
    int stablePlayerTicks = 0;
    bool havePosition = false;
    totk::core::WorldPosition lastPosition{};
    float lastVelocity[3]{};
    std::uint64_t lastGameTime = 0;
};

struct RefreshOutcome {
    bool resolved = false;   // player available and its pose is finite
    bool poseFinite = true;  // false only when a live player pose was non-finite
    pure::Pose pose{};
};

void initialize(std::uintptr_t mainBase);
const PlayerBridgeState& state();

[[nodiscard]] inline bool havePlayer() { return state().playerAddress != 0; }

void* playerActor();

struct PlayerIdentity {
    std::uintptr_t address = 0;
    std::uint32_t incarnation = 0;
};
bool readPlayerIdentity(PlayerIdentity& out);

bool readOutfit(const pure::OutfitSpeedProfile& profile, pure::OutfitSnapshot& out);

RefreshOutcome refresh(InvalidateFn invalidate, float teleportResetMeters);

void onGenerationInvalidated();

bool readPose(pure::Pose& out);
bool forcePose(const pure::Pose& pose);

float liveSpeed();
bool readEngineVelocity(float out[3]);
bool clearLinearVelocity();

bool climbSensorEngaged();
bool nativeClimbing();
bool readUiPlayerState(std::uint32_t& out);
bool readUprightY(float& out);

}  // namespace self_recall::world
