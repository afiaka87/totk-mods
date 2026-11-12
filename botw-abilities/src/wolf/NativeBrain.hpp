// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "NativeActor.hpp"

namespace botw_abilities::wolf {

struct BrainTick { float delta{}, rate{}; };
using BrainUpdate = void(*)(const ActorReference&, BrainTick);

// The callback and its storage must last for the process; updates run on the actor's AI worker.
[[nodiscard]] bool installBrainApi(std::uintptr_t base, BrainUpdate update);
void selectBrainOwner(std::uint32_t generation = UINT32_MAX);
[[nodiscard]] bool isBrainOwner(std::uint32_t generation);

}
