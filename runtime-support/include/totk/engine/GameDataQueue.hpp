// SPDX-License-Identifier: MIT
// Copyright (C) Clay Mullis
#pragma once

#include "totk/core/Result.hpp"
#include "totk/engine/Pointer.hpp"
#include "totk/engine/Totk121Offsets.hpp"

namespace totk::engine {

enum class GameDataError : std::uint8_t {
    None,
    MainImageUnavailable,
    ManagerHolderUnavailable,
    ManagerUnavailable,
    QueueStoreUnavailable,
    QueueUnallocated,
    QueueFull,
    GameplayNotReady,
    EngineFunctionUnavailable,
    FlagUnavailable,
};

struct GameDataQueueRoom {
    std::int32_t capacity = 0;
    std::int32_t writeIndex = 0;
    std::int32_t requestedPushes = 0;
    std::int32_t reservedHeadroom = layout::kGameDataQueueHeadroom;
};

using GameDataQueueResult = core::Result<GameDataQueueRoom, GameDataError>;

[[nodiscard]] inline GameDataQueueResult
inspectGameDataQueue(std::uintptr_t store, std::int32_t pushes,
                     std::int32_t headroom = layout::kGameDataQueueHeadroom) {
    if (!isPlausibleAddress(store) || pushes < 0 || headroom < 0) {
        return GameDataQueueResult::failure(GameDataError::QueueStoreUnavailable);
    }
    const auto capacity = readMemory<std::int32_t>(
        store + layout::kGameDataQueueCapacity);
    const auto buffer = readMemory<std::uintptr_t>(
        store + layout::kGameDataQueueBuffer);
    const auto writeIndex = static_cast<std::int32_t>(
        readMemory<std::uint32_t>(store + layout::kGameDataQueueControl) &
        layout::kGameDataQueueIndexMask);
    if (capacity <= 0 || !isPlausibleAddress(buffer)) {
        return GameDataQueueResult::failure(GameDataError::QueueUnallocated);
    }

    const GameDataQueueRoom room{capacity, writeIndex, pushes, headroom};
    if (writeIndex > capacity || pushes > capacity - writeIndex - headroom) {
        return GameDataQueueResult{room, GameDataError::QueueFull, false};
    }
    return GameDataQueueResult::success(room);
}

enum class GameDataWriteReadiness : std::uint8_t {
    NotReady,
    LiveGameplayAndWorkerAlive,
};

} // namespace totk::engine
