// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Draw the chain and reticle after opaque scene rendering.
#pragma once

#include <stdint.h>

namespace zonai_hookshot::render {
// Verifies the hook site by its opening bytes; a mismatch logs and disables the pass.
void installShaderPass(uintptr_t mainBase);

}  // namespace zonai_hookshot::render
