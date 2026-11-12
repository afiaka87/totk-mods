// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "totk/engine/GameDataQueue.hpp"

namespace botw_abilities::champions::items {
bool install(std::uintptr_t base);
void service(totk::engine::GameDataWriteReadiness readiness, std::uint64_t tick);
} // namespace botw_abilities::champions::items
