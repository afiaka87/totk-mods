// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>

namespace totk::render {
// A hooked function entry: its image offset and its first two original words.
struct PfxSite {
    std::uintptr_t offset;
    std::uint32_t first;
    std::uint32_t second;
};
inline constexpr PfxSite kPfxSite121{0xc30c48, 0xd104c3ff, 0xa90d7bfd};
inline constexpr std::uintptr_t kPfxHookOffset = kPfxSite121.offset;
enum class PfxEntryKind { Unsupported, Vanilla, Branch, Absolute };
struct PfxEntry {
    PfxEntryKind kind{PfxEntryKind::Unsupported};
    std::uintptr_t previous{};
};

// Accepts exlaunch's hook patch forms: an in-range B, or an absolute jump laid out for the entry's alignment.
inline constexpr PfxEntry decodeDetour(std::uintptr_t site, const std::uint32_t* words) {
    std::uintptr_t previous{};
    PfxEntryKind kind{};
    const bool nopForm = (site & 7) == 4;
    if ((words[0] & 0xfc000000u) == 0x14000000u) {
        const auto immediate = std::int64_t(words[0] & 0x03ffffffu);
        const auto displacement = (immediate >= 0x02000000 ? immediate - 0x04000000 : immediate) * 4;
        previous = site + displacement;
        kind = PfxEntryKind::Branch;
    } else if (!nopForm && words[0] == 0x58000051u && words[1] == 0xd61f0220u) {
        previous = std::uintptr_t(words[2]) | (std::uintptr_t(words[3]) << 32);
        kind = PfxEntryKind::Absolute;
    } else if (nopForm && words[0] == 0xd503201fu && words[1] == 0x58000051u &&
               words[2] == 0xd61f0220u) {
        previous = std::uintptr_t(words[3]) | (std::uintptr_t(words[4]) << 32);
        kind = PfxEntryKind::Absolute;
    } else {
        return {};
    }
    // Refuse null, unaligned, and destinations inside the overwritten entry.
    if (!previous || (previous & 3) || (previous >= site && previous < site + 20))
        return {};
    return {kind, previous};
}

inline constexpr PfxEntry decodePfxEntry(std::uintptr_t site, const std::uint32_t* words,
                                       std::uint32_t first, std::uint32_t second) {
    if (words[0] == first && words[1] == second)
        return {PfxEntryKind::Vanilla, 0};
    return decodeDetour(site, words);
}

// TotK 1.2.1's draw-extension entry.
inline constexpr PfxEntry decodePfxEntry(std::uintptr_t site, const std::uint32_t* words) {
    return decodePfxEntry(site, words, kPfxSite121.first, kPfxSite121.second);
}
}
