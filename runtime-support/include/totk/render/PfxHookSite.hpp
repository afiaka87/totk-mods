// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>

namespace totk::render {
inline constexpr std::uintptr_t kPfxHookOffset = 0xc30c48;
enum class PfxEntryKind { Unsupported, Vanilla, Branch, Absolute };
struct PfxEntry {
    PfxEntryKind kind{PfxEntryKind::Unsupported};
    std::uintptr_t previous{};
};

// TotK 1.2.1 prologue and the two patches emitted by vendored exlaunch
// nx64/hook_impl.cpp::HookFuncImpl. This entry is eight-byte aligned.
inline constexpr PfxEntry decodePfxEntry(std::uintptr_t site,
                                       const std::uint32_t* words) {
    if (words[0] == 0xd104c3ff && words[1] == 0xa90d7bfd)
        return {PfxEntryKind::Vanilla, 0};
    std::uintptr_t previous{};
    PfxEntryKind kind{};
    if ((words[0] & 0xfc000000u) == 0x14000000u) {
        const auto immediate = std::int64_t(words[0] & 0x03ffffffu);
        const auto displacement = (immediate >= 0x02000000 ? immediate - 0x04000000 : immediate) * 4;
        previous = site + displacement;
        kind = PfxEntryKind::Branch;
    } else if (words[0] == 0x58000051u && words[1] == 0xd61f0220u) {
        previous = std::uintptr_t(words[2]) | (std::uintptr_t(words[3]) << 32);
        kind = PfxEntryKind::Absolute;
    } else {
        return {};
    }
    // Refuse null, unaligned, and destinations inside the overwritten entry.
    if (!previous || (previous & 3) || (previous >= site && previous < site + 20))
        return {};
    return {kind, previous};
}
}
