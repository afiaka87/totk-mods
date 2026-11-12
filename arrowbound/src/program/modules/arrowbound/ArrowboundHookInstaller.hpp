// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87

#pragma once

#include <cstdint>

namespace arrowbound::hooks {
void install(std::uintptr_t mainBase, bool installShared = true);
}
