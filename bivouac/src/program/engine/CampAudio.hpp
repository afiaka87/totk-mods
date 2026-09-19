// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstdint>

namespace bivouac::audio {

void initialize(std::uintptr_t mainBase);

// Finds the interface speaker ahead of the first cue.
void prime();
bool ready();

// One-shot through the interface speaker; true only when the game handed back a live handle.
bool playCue(const char* cueName);

}  // namespace bivouac::audio
