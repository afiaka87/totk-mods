// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "NativeActor.hpp"

namespace botw_abilities::wolf {
bool installObservation(std::uintptr_t base);
void logObservation(const ActorReference& actor, std::uintptr_t base);
}
