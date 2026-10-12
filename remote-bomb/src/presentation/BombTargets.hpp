// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/render/GlowTargets.hpp"
#include <nvn/nvn.h>
#include <cstdint>

namespace remote_bomb::targets {
// The shared glow images: Bright at full size, then the half and quarter size blur chain.
using Image = totk::render::GlowImage;
constexpr unsigned kWidth = totk::render::kGlowSceneWidth, kHeight = totk::render::kGlowSceneHeight;
bool initialize(NVNdevice *, PFNNVNDEVICEGETPROCADDRESSPROC, std::uintptr_t);
// After the game's own framebuffer bind: draws onto the game's frame, with Bright and the private depth beside it when the frame fits them. Returns false when the frame cannot be found; bloom reports whether Bright is bound.
bool begin(NVNcommandBuffer *, void *context, unsigned width, unsigned height, bool &bloom);
void emission(NVNcommandBuffer *);
void bind(NVNcommandBuffer *, Image, unsigned width, unsigned height);
void meshDepth(NVNcommandBuffer *, bool enabled);
void effectsDepth(NVNcommandBuffer *);
NVNtextureHandle handle(Image, int sampler);
}
