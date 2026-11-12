// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "CryonisWorld.hpp"
namespace cryonis::runtime {
engine::WorldContext world();
void clearTarget(bool clearRequested);
void erase(unsigned slot, const char *reason);
void requestSpawn();
void updatePillars(float frames);
void select();
void reset(const char *reason);
}
