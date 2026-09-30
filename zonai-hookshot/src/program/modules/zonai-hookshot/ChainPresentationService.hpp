// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Publish values; the render thread does not dereference Player or camera pointers.
#pragma once

#include "HookshotRuntime.hpp"

namespace zonai_hookshot::presentation {
// Lift only the visual marker off the hit triangle to avoid z-fighting.
constexpr float kAnchorVisualLift = 0.03f;

void publishChain(HookshotRuntime& runtime);
void publishInvisible();

}  // namespace zonai_hookshot::presentation
