// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "PfxHookSite.hpp"
#include <lib.hpp>
#if defined(TOTK_PORT) && TOTK_PORT
#include "totk/port/CodeCheck.hpp"
#endif

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

#if defined(TOTK_PORT) && TOTK_PORT
inline namespace ported {
// An entry named by its 1.2.1 place and words; on 1.4.3 the place translates and the recorded words apply.
inline bool installPfxHook(std::uintptr_t mainBase, const port::Site& pfx, PfxCallback callback,
                           PfxCallback& previous, const char* owner) {
    std::uint32_t first = pfx.first, second = pfx.second;
    if (port::is143()) {
        const std::uint32_t* words =
            pfx.site.available() ? port::recordedWords(pfx.site.source()) : nullptr;
        if (!words) {
            Logging.Log("[%s] PFX off on 1.4.3: the entry has no recorded 1.4.3 place", owner);
            return false;
        }
        first = words[0];
        second = words[1];
    }
    const PfxSite site{static_cast<std::uintptr_t>(static_cast<std::ptrdiff_t>(pfx.site)), first,
                       second};
    return totk::render::installPfxHook(mainBase, site, callback, previous, owner);
}

// The draw-extension entry (TotK 1.2.1's place, translated on 1.4.3).
inline bool installPfxHook(std::uintptr_t mainBase, PfxCallback callback,
                           PfxCallback& previous, const char* owner) {
    return installPfxHook(mainBase,
                          port::Site{static_cast<std::uint32_t>(kPfxSite121.offset),
                                     kPfxSite121.first, kPfxSite121.second},
                          callback, previous, owner);
}
}  // inline namespace ported
#else
// TotK 1.2.1's draw-extension entry.
inline bool installPfxHook(std::uintptr_t mainBase, PfxCallback callback,
                           PfxCallback& previous, const char* owner) {
    return installPfxHook(mainBase, kPfxSite121, callback, previous, owner);
}
#endif
}
