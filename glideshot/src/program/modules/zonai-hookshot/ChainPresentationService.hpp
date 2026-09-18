// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Publishes the chain snapshot by value so the render thread never follows a Player or camera
// pointer.
#pragma once

#include "HookshotRuntime.hpp"

namespace zonai_hookshot::presentation {
// Presentation-only lift off the triangle to avoid z-fighting; the logical anchor stays the exact
// hit.
constexpr float kAnchorVisualLift = 0.03f;

void publishChain(HookshotRuntime& runtime);
void publishInvisible();

}  // namespace zonai_hookshot::presentation
