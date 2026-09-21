// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstdint>

namespace arrowbound::hooks {
void install(std::uintptr_t mainBase, bool installShared = true);
}
