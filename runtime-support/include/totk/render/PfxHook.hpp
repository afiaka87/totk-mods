// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "PfxHookSite.hpp"
#include <lib.hpp>

namespace totk::render {
using PfxCallback = std::uint64_t (*)(void*, void*);

// Startup only, before rendering begins. Never relocate an existing detour's
// embedded pointer: preserve its destination directly as our predecessor.
inline bool installPfxHook(std::uintptr_t mainBase, const PfxSite& pfx, PfxCallback callback,
                           PfxCallback& previous, const char* owner) {
    if (previous) return true;
    const auto site = mainBase + pfx.offset;
    const auto* words = reinterpret_cast<const std::uint32_t*>(site);
    const auto entry = decodePfxEntry(site, words, pfx.first, pfx.second);
    if (entry.kind == PfxEntryKind::Unsupported) {
        Logging.Log("[%s] PFX refused bytes=%08x,%08x", owner, words[0], words[1]);
        return false;
    }
    if (entry.kind == PfxEntryKind::Vanilla) {
        previous = exl::hook::Hook(site, callback, true);
    } else {
        MemoryInfo info{};
        std::uint32_t page{};
        const auto query = svcQueryMemory(&info, &page, entry.previous);
        if (entry.previous == reinterpret_cast<std::uintptr_t>(callback) ||
            query || (info.perm & Perm_Rx) != Perm_Rx) {
            Logging.Log("[%s] PFX refused target=%p query=%08x perm=%x", owner,
                        reinterpret_cast<void*>(entry.previous), query, info.perm);
            return false;
        }
        previous = reinterpret_cast<PfxCallback>(entry.previous);
        exl::hook::Hook(site, callback, false);
    }
    Logging.Log("[%s] PFX installed kind=%u previous=%p", owner,
                static_cast<unsigned>(entry.kind), reinterpret_cast<void*>(previous));
    return true;
}

// TotK 1.2.1's draw-extension entry.
inline bool installPfxHook(std::uintptr_t mainBase, PfxCallback callback,
                           PfxCallback& previous, const char* owner) {
    return installPfxHook(mainBase, kPfxSite121, callback, previous, owner);
}
}
