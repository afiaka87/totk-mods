// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "CryonisHookContext.hpp"
#include "CryonisPresentation.hpp"
namespace cryonis::runtime {
void initialize(std::uintptr_t base);
void tick(void *device);
bool presentation(Presentation &out, bool hud = false);
engine::HookContext hooks();
}
