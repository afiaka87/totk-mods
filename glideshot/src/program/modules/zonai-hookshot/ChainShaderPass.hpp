// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Screen-space chain pass: a trampoline on the engine's particle-effect draw entry, NVN entry points resolved from
// the game's device, one three-vertex draw evaluating the helix and reticle per pixel.
#pragma once

#include <stdint.h>

namespace zonai_hookshot::render {
// Verifies the hook site by its opening bytes; a mismatch logs and disables the pass.
void installShaderPass(uintptr_t mainBase);

}  // namespace zonai_hookshot::render
