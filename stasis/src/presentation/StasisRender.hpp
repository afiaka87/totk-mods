// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
namespace stasis::render {
void install();
// Draws the effects with the selected object's masks, from the surface pass.
void drawSurface(void *drawContext, void *context, void *buffers);
} // namespace stasis::render
