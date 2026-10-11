// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/engine/Pointer.hpp"
#include <lib.hpp>
#include <cstdint>
#include <span>

namespace totk::engine {
// The first two instructions of a game function a mod hooks, calls or reads; a zero second word
// checks only the first.
struct CodeSite {
    std::uintptr_t offset;
    std::uint32_t first, second;
};

// An entry another mod in the same module already hooked starts with a near jump to that hook;
// the rest of the function is unchanged, so the second word is still checked.
inline constexpr bool entryMatches(std::uint32_t actual, std::uint32_t expected) {
    return actual == expected || (actual & 0xfc000000u) == 0x14000000u;
}

// True when every site matches the running game; the first mismatch is logged as "[owner] tag".
inline bool codeMatches(std::uintptr_t base, std::span<const CodeSite> sites, const char *owner,
                        const char *tag) {
    for (const auto site : sites) {
        const auto first = readMemory<std::uint32_t>(base + site.offset);
        const auto second = readMemory<std::uint32_t>(base + site.offset + 4);
        if (!entryMatches(first, site.first) || (site.second && second != site.second)) {
            Logging.Log("[%s] %s offset=%p expected=%08x,%08x actual=%08x,%08x", owner, tag,
                        reinterpret_cast<void *>(site.offset), site.first, site.second, first,
                        second);
            return false;
        }
    }
    return true;
}
} // namespace totk::engine
