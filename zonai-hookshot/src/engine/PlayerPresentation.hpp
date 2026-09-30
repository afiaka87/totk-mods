// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
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
bool usesHandOrigin();
void steerGlide(void* controller);
void observeClimb(void* action);
} // namespace playerPresentation
} // namespace zonai_hookshot
