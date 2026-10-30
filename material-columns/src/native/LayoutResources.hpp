// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>

namespace material_columns::native {
// Install only after the complete adapter's preflight has succeeded.
bool installLayoutResources(std::uintptr_t mainBase, bool validateOnly = false);
}
