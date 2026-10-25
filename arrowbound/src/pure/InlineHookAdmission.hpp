// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/render/PfxHookSite.hpp"

namespace arrowbound::pure {
// A one-instruction inline site can chain an existing near B, not a long jump.
inline constexpr bool inlineHookable(std::uintptr_t at, std::uint32_t actual,
                                     std::uint32_t expected) {
    if (actual == expected) return true;
    if ((actual & 0xfc000000u) != 0x14000000u) return false;
    return totk::render::decodeDetour(at, &actual).kind == totk::render::PfxEntryKind::Branch;
}
}
