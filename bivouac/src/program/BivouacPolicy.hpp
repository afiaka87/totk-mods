// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <stdint.h>

namespace bivouac::policy {

// CAMP stays zero: pre-tier ledger records stored zero in the byte that became the tier.
enum class CampTier : uint8_t { CAMP = 0, BIVY = 1, BASE = 2 };

enum class ClimbObservation : uint8_t {
    LocalSensor,
    NotClimbing,
    Climbing,
};

constexpr bool resolveClimbing(ClimbObservation observation,
                               bool localSensorValue) {
    if (observation == ClimbObservation::LocalSensor) {
        return localSensorValue;
    }
    return observation == ClimbObservation::Climbing;
}

// Trigger items grow rarer with each tier.
inline constexpr const char* kBivyItem = "Item_Mushroom_F"; // Hearty Truffle
inline constexpr const char* kCampItem = "Item_Mushroom_N"; // Big Hearty Truffle
inline constexpr const char* kBaseItem = "Item_PlantGet_C"; // Big Hearty Radish

struct TierMatch {
    bool matched = false;
    CampTier tier = CampTier::CAMP;
};

constexpr bool textEquals(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) return false;
    for (int i = 0; i < 64; ++i) {
        if (a[i] != b[i]) return false;
        if (a[i] == '\0') return true;
    }
    return false;
}

constexpr TierMatch tierForItem(const char* name) {
    if (textEquals(name, kCampItem)) return {true, CampTier::CAMP};
    if (textEquals(name, kBivyItem)) return {true, CampTier::BIVY};
    if (textEquals(name, kBaseItem)) return {true, CampTier::BASE};
    return {};
}

// The climb-physics routine pulses every climb frame; a pulse within the grace means Link is climbing.
constexpr bool heartbeatClimbing(int tick, int lastPulseTick, int graceTicks) {
    const int sincePulse = tick - lastPulseTick;
    return sincePulse >= 0 && sincePulse <= graceTicks;
}

constexpr bool climbIsRecent(int tick, int lastClimbFlagTick, int climbEndTick,
                             int flagWindowTicks, int menuGraceTicks) {
    const int sinceFlag = tick - lastClimbFlagTick;
    const int sinceEnd = tick - climbEndTick;
    return (sinceFlag >= 0 && sinceFlag <= flagWindowTicks)
        || (sinceEnd >= 0 && sinceEnd <= menuGraceTicks);
}

consteval bool contracts() {
    const TierMatch camp = tierForItem(kCampItem);
    const TierMatch bivy = tierForItem(kBivyItem);
    const TierMatch base = tierForItem(kBaseItem);
    if (!camp.matched || camp.tier != CampTier::CAMP) return false;
    if (!bivy.matched || bivy.tier != CampTier::BIVY) return false;
    if (!base.matched || base.tier != CampTier::BASE) return false;
    if (tierForItem("Item_Fruit_B").matched) return false; // Wildberry is not a ship trigger.
    if (tierForItem(nullptr).matched || tierForItem("").matched) return false;
    if (static_cast<uint8_t>(CampTier::CAMP) != 0) return false;
    if (!resolveClimbing(ClimbObservation::LocalSensor, true)) return false;
    if (resolveClimbing(ClimbObservation::LocalSensor, false)) return false;
    if (resolveClimbing(ClimbObservation::NotClimbing, true)) return false;
    if (!resolveClimbing(ClimbObservation::Climbing, false)) return false;

    if (!climbIsRecent(100, 67, -1000, 33, 165)) return false;  // exact flag boundary
    if (climbIsRecent(100, 66, -1000, 33, 165)) return false;
    if (!climbIsRecent(1000, -1000, 835, 33, 165)) return false; // exact menu boundary
    if (climbIsRecent(1000, -1000, 834, 33, 165)) return false;
    if (climbIsRecent(100, 101, 101, 33, 165)) return false;     // future/stale data
    return true;
}

static_assert(contracts(), "Bivouac product policy contracts must hold");

} // namespace bivouac::policy
