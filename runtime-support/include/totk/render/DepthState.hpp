// SPDX-License-Identifier: GPL-2.0-only
//
// Shared TotK runtime support — the world seam's depth contract.
// Copyright (C) Clay Mullis
//
// Drawing at the end of the solid pass is only half of "depth-correct". The
// other half is the render state the draw runs under, and the engine's own
// state is not it: the seam has to install depth TESTING (so a line behind a
// hill is hidden by the hill) with depth WRITES off (so the reading is laid
// over the world rather than becoming a surface that hides later translucent
// effects), and then put the engine's state back before the frame continues.
//
// Two 128-byte graphics contexts are built once at configure time — one holding
// the seam's state, one holding the engine's untouched default — because
// constructing them per frame would cost a call into the engine on the render
// thread for a value that never changes.
//
// Promoted out of porter-bridges-redux on 2026-08-21 with the rest of the seam.
#pragma once

#include <cstdint>

namespace agl {
class DrawContext;
}

namespace totk::render {

// Builds both graphics contexts. Idempotent; the first caller wins.
bool configureDepthState(std::uintptr_t mainBase);

// Installs depth-tested / write-disabled state on the draw context. False means
// nothing was installed and the caller must not draw.
bool applyDepthTestedOverlay(agl::DrawContext* drawCtx);

// Puts the engine's own default state back. Always paired with a successful
// applyDepthTestedOverlay.
void restoreSceneState(agl::DrawContext* drawCtx);

}  // namespace totk::render
