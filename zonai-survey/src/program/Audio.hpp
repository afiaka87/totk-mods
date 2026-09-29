// SPDX-License-Identifier: MIT

// Copyright (C) Clay Mullis

#pragma once

#include <cstdint>

namespace audio {

void init(uintptr_t mainBase);

void playCue(const char* cueName);

}
