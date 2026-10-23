// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "SavedState.hpp"

namespace botw_abilities::champions::hud {
bool install(std::uintptr_t base);
void reset();
void hide();
void service(const SavedSnapshot& saved, const std::array<Context,4>& contexts);
} // namespace botw_abilities::champions::hud
