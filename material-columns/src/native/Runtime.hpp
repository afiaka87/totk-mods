// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>
namespace material_columns::native {
using NativeUpdate = std::uintptr_t (*)(std::uintptr_t child);
bool installRuntime(std::uintptr_t mainBase, bool validateOnly = false);
std::uintptr_t updateMaterialShortcut(std::uintptr_t child, NativeUpdate original);
void forgetMaterialScreen(std::uintptr_t screen);
bool materialColumnsActive(std::uintptr_t screen);
}
