// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>
namespace material_columns::native {
bool installSortPanel(std::uintptr_t mainBase, bool validateOnly = false);
void refreshSortPanel(std::uintptr_t screen);
}
