// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include <cstdint>
#include <limits>

namespace arrowbound::engine {

template <class Valid, class Pointer, class Integer>
std::uintptr_t selectedKeyItemSlot(std::uintptr_t control, std::uint32_t index,
                                   Valid valid, Pointer pointer, Integer integer) {
    if (!valid(control)) return 0;
    const auto pages = pointer(control + 0x60);
    if (!valid(pages)) return 0;
    const auto first = pointer(pages);
    if (!valid(first)) return 0;
    const auto grid = pointer(first + 8);
    if (!valid(grid)) return 0;
    const std::uint64_t cells = std::uint64_t(integer(grid + 0x30)) * integer(grid + 0x34);
    const auto pageCount = integer(control + 0x58);
    if (!cells || !pageCount || cells + index > std::uint32_t(std::numeric_limits<int>::max()))
        return 0;
    const auto adjusted = cells + index; // Native key-items page follows the sage page.
    const auto page = pointer(pages + ((adjusted / cells) % pageCount) * 8);
    if (!valid(page)) return 0;
    const auto cell = adjusted % cells;
    if (cell >= integer(page + 0x40)) return 0;
    const auto slots = pointer(page + 0x48);
    if (!valid(slots)) return 0;
    const auto slot = pointer(slots + cell * 8);
    return valid(slot) ? slot : 0;
}

} // namespace arrowbound::engine
