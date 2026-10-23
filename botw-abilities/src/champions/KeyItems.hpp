// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/engine/GameDataQueue.hpp"

namespace botw_abilities::champions::items {
bool install(std::uintptr_t base);
void service(totk::engine::GameDataWriteReadiness readiness, std::uint64_t tick);
} // namespace botw_abilities::champions::items
