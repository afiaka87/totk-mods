// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/engine/Pointer.hpp"
#include <lib.hpp>
#include <cstdint>
#include <initializer_list>
#include <span>

namespace totk::engine {
// Expected function-entry instructions; a zero second word checks only the first.
struct CodeSite {
    std::uintptr_t offset;
    std::uint32_t first, second;
};

// A near hook changes only the first word; codeMatches checks the remaining entry.
inline constexpr bool entryMatches(std::uint32_t actual, std::uint32_t expected) {
    return actual == expected || (actual & 0xfc000000u) == 0x14000000u;
}

// A near hook replaces only word zero, leaving an earlier absolute hook's tail.
inline bool chainedAbsoluteTail(std::uintptr_t site, std::uint32_t first) {
    if ((first & 0xfc000000u) != 0x14000000u) return false;
    const auto second = readMemory<std::uint32_t>(site + 4);
    const bool hasAlignmentNop = (site & 7) == 4;
    if (hasAlignmentNop ? (second != 0x58000051u ||
                           readMemory<std::uint32_t>(site + 8) != 0xd61f0220u)
                        : second != 0xd61f0220u) return false;
    const auto previousTarget = readMemory<std::uintptr_t>(site + (hasAlignmentNop ? 12 : 8));
    const auto immediate = std::int64_t(first & 0x03ffffffu);
    const auto displacement = (immediate >= 0x02000000 ? immediate - 0x04000000 : immediate) * 4;
    const auto currentTarget = site + displacement;
    for (const auto target : {previousTarget, currentTarget}) {
        if (!target || (target & 3) || (target >= site && target < site + 20)) return false;
        MemoryInfo info{};
        std::uint32_t page{};
        if (svcQueryMemory(&info, &page, target) || (info.perm & Perm_Rx) != Perm_Rx) return false;
    }
    return true;
}

// True when every site matches the running game; the first mismatch is logged as "[owner] tag".
inline bool codeMatches(std::uintptr_t base, std::span<const CodeSite> sites, const char *owner,
                        const char *tag) {
    for (const auto site : sites) {
        const auto first = readMemory<std::uint32_t>(base + site.offset);
        const auto second = readMemory<std::uint32_t>(base + site.offset + 4);
        if (!entryMatches(first, site.first) ||
            (site.second && second != site.second && !chainedAbsoluteTail(base + site.offset, first))) {
            Logging.Log("[%s] %s offset=%p expected=%08x,%08x actual=%08x,%08x", owner, tag,
                        reinterpret_cast<void *>(site.offset), site.first, site.second, first,
                        second);
            return false;
        }
    }
    return true;
}
} // namespace totk::engine
