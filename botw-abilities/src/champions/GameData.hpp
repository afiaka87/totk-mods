// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once

#include "SavedState.hpp"
#include "StateTransfer.hpp"
#include "totk/engine/GameDataQueue.hpp"

namespace botw_abilities::champions::data {
bool install(std::uintptr_t base);
SavedState& state();
StateTransfer flush(totk::engine::GameDataWriteReadiness readiness);
} // namespace botw_abilities::champions::data
