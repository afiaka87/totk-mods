// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
namespace stasis::render {
void install();
// Draws the effects with the selected object's masks, from the surface pass.
void drawSurface(void *drawContext, void *context, void *buffers);
} // namespace stasis::render
