// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/render/GlowTargets.hpp"
#include <nvn/nvn.h>
#include <cstdint>

namespace remote_bomb::targets {
// The shared glow images: Bright at full size, then the half and quarter size blur chain.
using Image = totk::render::GlowImage;
constexpr unsigned kWidth = totk::render::kGlowWidth, kHeight = totk::render::kGlowHeight;
bool initialize(NVNdevice *, PFNNVNDEVICEGETPROCADDRESSPROC, std::uintptr_t);
// Binds the game's frame plus Bright and the private depth when they fit; false when the frame
// cannot be found, and bloom reports whether Bright is bound.
bool begin(NVNcommandBuffer *, void *context, unsigned width, unsigned height, bool &bloom);
void bind(NVNcommandBuffer *, Image, unsigned width, unsigned height);
void meshDepth(NVNcommandBuffer *, bool enabled);
void effectsDepth(NVNcommandBuffer *);
NVNtextureHandle handle(Image, int sampler);
}
