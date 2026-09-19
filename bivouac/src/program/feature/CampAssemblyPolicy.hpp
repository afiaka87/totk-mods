// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstdint>

namespace bivouac::feature {

enum class AssemblySlotState : std::uint8_t {
    Unspawned = 0,
    SpawnRequested,
    Alive,
    Dead,
};

struct SpawnFacts {
    std::uint8_t tierMask = 0;
    std::uint8_t siteTier = 0;
    bool waterSite = false;
    std::uint8_t backerLevel = 0;
    float backerShift = 0.0f;
    float minimumBackerShift = 0.0f;
    bool hasDependency = false;
    bool dependencyAlive = false;
    AssemblySlotState slotState = AssemblySlotState::Unspawned;
    std::uint8_t failures = 0;
    std::uint8_t maximumFailures = 0;
    int currentTick = 0;
    int retryAtTick = 0;
    float distanceToPlayer = 0.0f;
    float spawnRadius = 0.0f;
    bool isSentinel = false;
    bool sentinelReady = false;
};

constexpr bool shouldSpawn(const SpawnFacts& facts) {
    if (((facts.tierMask >> facts.siteTier) & 1u) == 0) {
        return false;
    }
    if (facts.waterSite && facts.backerLevel != 0) {
        return false;
    }
    if (facts.backerLevel != 0
        && facts.backerShift < facts.minimumBackerShift) {
        return false;
    }
    if (facts.hasDependency && !facts.dependencyAlive) {
        return false;
    }
    if (facts.slotState == AssemblySlotState::Alive
        || facts.slotState == AssemblySlotState::SpawnRequested) {
        return false;
    }
    if (facts.failures >= facts.maximumFailures
        || facts.currentTick < facts.retryAtTick
        || facts.distanceToPlayer > facts.spawnRadius) {
        return false;
    }
    return facts.isSentinel || facts.sentinelReady;
}

constexpr int retryAtTick(
    int currentTick, int retryTicks,
    std::uint8_t failures, std::uint8_t maximumFailures) {
    return currentTick + retryTicks
        * (failures >= maximumFailures ? 10 : 1);
}

} // namespace bivouac::feature
