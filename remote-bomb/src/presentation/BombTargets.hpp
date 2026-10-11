// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <nvn/nvn.h>
#include <cstdint>

namespace remote_bomb::targets {
enum Image : unsigned { Surface, Bright, NarrowA, NarrowB, WideA, WideB, Depth, Count };
constexpr unsigned kWidth = 1920, kHeight = 1080;
bool initialize(NVNdevice *, PFNNVNDEVICEGETPROCADDRESSPROC, std::uintptr_t);
void begin(NVNcommandBuffer *, unsigned width, unsigned height);
void bind(NVNcommandBuffer *, Image, unsigned width, unsigned height);
void meshDepth(NVNcommandBuffer *, bool enabled);
void effectsDepth(NVNcommandBuffer *);
NVNtextureHandle handle(Image, int sampler);
}
