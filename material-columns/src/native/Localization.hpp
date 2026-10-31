// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>
namespace material_columns::native::locale {
void loadOnce();
const char* language();
const char* label(std::uint64_t group, const char* fallback);
}
