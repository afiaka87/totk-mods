// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>

namespace material_columns {
// FNV-1a over UTF-8 actor/group identifiers, shared with tools/build_catalog.py.
// Build-time collisions are rejected. Runtime records retain the native slot token.
constexpr std::uint64_t identity(const char* text) {
    std::uint64_t value = 0xcbf29ce484222325ULL;
    for (; *text; ++text) {
        value = (value ^ static_cast<unsigned char>(*text)) * 0x100000001b3ULL;
    }
    return value;
}
} // namespace material_columns
