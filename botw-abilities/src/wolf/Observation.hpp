// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "NativeActor.hpp"

namespace botw_abilities::wolf {
bool installObservation(std::uintptr_t base);
void logObservation(const ActorReference& actor, std::uintptr_t base);
}
