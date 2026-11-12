// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include <cstdint>
#include "ArrowboundPure.hpp"

namespace zonai_hookshot {
struct HookshotRuntime;
namespace playerPresentation {
void install(std::uintptr_t mainBase);
void update(const HookshotRuntime& runtime);
void reset();
bool handOrigin(pure::Vec3& out);
// Zero when `out` was written; otherwise the reasons the hand position is unavailable.
inline constexpr unsigned kHandNotInstalled = 1, kHandNotTracked = 2, kHandNotPublished = 4,
                          kHandOldGeneration = 8, kHandStale = 16;
unsigned handOriginState(pure::Vec3& out);
bool usesHandOrigin();
void steerGlide(void* controller);
void observeClimb(void* action);
} // namespace playerPresentation
} // namespace zonai_hookshot
