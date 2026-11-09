// SPDX-License-Identifier: MIT
// Copyright (C) Clay Mullis
// Guarded flat-int GameData access for TotK 1.2.1.

#pragma once

#include "totk/engine/GameDataQueue.hpp"

#include <cstdint>

namespace totk::engine {

using GameDataManagerResult =
    core::Result<std::uintptr_t, GameDataError>;

[[nodiscard]] inline GameDataManagerResult
resolveGameDataManagerFromHolder(std::uintptr_t holderSlot) {
    if (!isPlausibleAddress(holderSlot)) {
        return GameDataManagerResult::failure(
            GameDataError::ManagerHolderUnavailable);
    }
    const auto holder = readMemory<std::uintptr_t>(holderSlot);
    if (!isPlausibleAddress(holder)) {
        return GameDataManagerResult::failure(
            GameDataError::ManagerHolderUnavailable);
    }
    const auto manager = readMemory<std::uintptr_t>(holder);
    if (!isPlausibleAddress(manager)) {
        return GameDataManagerResult::failure(GameDataError::ManagerUnavailable);
    }
    return GameDataManagerResult::success(manager);
}

using GameDataGetIntFunction =
    std::uint32_t (*)(std::uintptr_t, std::int32_t*, std::uint32_t);
using GameDataSetIntFunction =
    std::uint32_t (*)(std::uintptr_t, std::int32_t, std::uint32_t);

struct GameDataFunctions {
    GameDataGetIntFunction getInt = nullptr;
    GameDataSetIntFunction setInt = nullptr;
};

class GameDataService {
public:
    GameDataService(std::uintptr_t managerHolderSlot, GameDataFunctions functions)
        : managerHolderSlot_(managerHolderSlot), functions_(functions) {}

    [[nodiscard]] static GameDataService fromMainBase(std::uintptr_t mainBase) {
        if (!isPlausibleAddress(mainBase)) return GameDataService{0, {}};
        return GameDataService{
            mainBase + Totk121Offsets::kGameDataManagerIndirect.value,
            GameDataFunctions{
                reinterpret_cast<GameDataGetIntFunction>(
                    mainBase + Totk121Offsets::kGameDataGetInt.value),
                reinterpret_cast<GameDataSetIntFunction>(
                    mainBase + Totk121Offsets::kGameDataSetInt.value),
            },
        };
    }

    [[nodiscard]] core::Result<std::int32_t, GameDataError>
    readInt(std::uint32_t nameHash) const {
        const auto manager = resolveGameDataManagerFromHolder(managerHolderSlot_);
        if (!manager) {
            return core::Result<std::int32_t, GameDataError>::failure(manager.error);
        }
        if (!functions_.getInt) {
            return core::Result<std::int32_t, GameDataError>::failure(
                GameDataError::EngineFunctionUnavailable);
        }
        std::int32_t value = 0;
        if ((functions_.getInt(manager.value, &value, nameHash) & 1U) == 0) {
            return core::Result<std::int32_t, GameDataError>::failure(
                GameDataError::FlagUnavailable);
        }
        return core::Result<std::int32_t, GameDataError>::success(value);
    }

    [[nodiscard]] GameDataError writeInt(
        std::uint32_t nameHash, std::int32_t value,
        GameDataWriteReadiness readiness) const {
        if (readiness != GameDataWriteReadiness::LiveGameplayAndWorkerAlive) {
            return GameDataError::GameplayNotReady;
        }
        const auto manager = resolveGameDataManagerFromHolder(managerHolderSlot_);
        if (!manager) return manager.error;
        const auto queue =
            inspectGameDataQueue(manager.value + layout::kGameDataIntStore, 1);
        if (!queue) return queue.error;
        if (!functions_.setInt) return GameDataError::EngineFunctionUnavailable;
        functions_.setInt(manager.value, value, nameHash);
        return GameDataError::None;
    }

private:
    std::uintptr_t managerHolderSlot_ = 0;
    GameDataFunctions functions_{};
};

} // namespace totk::engine
