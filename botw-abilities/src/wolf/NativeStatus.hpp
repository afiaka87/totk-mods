// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "Companion.hpp"
#include "NativeActor.hpp"

namespace botw_abilities::wolf {

bool initializeStatusApi(std::uintptr_t base);

// Everything reads false when the actor lacks the matching game system.
[[nodiscard]] CompanionStatus readStatus(const ActorReference& actor);

}
