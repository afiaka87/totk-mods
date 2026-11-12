// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
// Code checks and startup for mods that name game code by its 1.2.1 offset (totk/port/Port.hpp).
#pragma once

#include "totk/engine/CodeCheck.hpp"
#include "totk/port/Generated.hpp"
#include "totk/port/Port.hpp"

#include <lib.hpp>

#include <cstdint>
#include <span>

namespace totk::port {

// Expected 1.2.1 words at a place (zero second word: check only the first); 1.4.3 compares the recorded words.
struct Site {
    Code site;
    std::uint32_t first, second;
};

// Identifies the running game for a standalone module. False leaves every ported feature off.
inline bool start(const char* owner) {
    const std::uintptr_t base = exl::util::modules::GetTargetStart();
    const std::size_t textSize = exl::util::GetMainModuleInfo().m_Text.m_Size;
    const std::uint32_t early = state.early;
    const bool ok = select(base, textSize, generated::table(), [base](std::uint32_t offset) {
        return *reinterpret_cast<const volatile std::uint32_t*>(base + offset);
    });
    if (ok) {
        Logging.Log("[%s] game %s", owner, is143() ? "1.4.3" : "1.2.1");
    } else if (early) {
        Logging.Log("[%s] game not served: %u addresses were resolved before the game was known", owner, early);
    } else {
        Logging.Log("[%s] unknown game build main_text=%zx; nothing installed", owner, textSize);
    }
    return ok;
}

// The first mismatch is logged as "[owner] tag". In Zonai Fieldworks the words predate any member's hook.
inline bool codeMatches(std::uintptr_t base, std::span<const Site> sites, const char* owner, const char* tag) {
    if (!supported()) {
        Logging.Log("[%s] %s unsupported game", owner, tag);
        return false;
    }
    for (const auto& site : sites) {
        std::uint32_t wantFirst = site.first, wantSecond = site.second;
        if (is143()) {
            const std::uint32_t* words = site.site.available() ? recordedWords(site.site.source()) : nullptr;
            if (!words) {
                Logging.Log("[%s] %s offset=%p has no 1.4.3 place", owner, tag,
                            reinterpret_cast<void*>(static_cast<std::uintptr_t>(site.site.source())));
                return false;
            }
            wantFirst = words[0];
            wantSecond = site.second ? words[1] : 0;
        }
        const std::uintptr_t address = base + site.site;
        const auto first = totk::suite::checkedCode(address);
        const auto second = totk::suite::checkedCode(address + 4);
        if (!totk::engine::entryMatches(first, wantFirst) ||
            (wantSecond && second != wantSecond && !totk::engine::chainedAbsoluteTail(address, first))) {
            Logging.Log("[%s] %s offset=%p expected=%08x,%08x actual=%08x,%08x", owner, tag,
                        reinterpret_cast<void*>(address - base), wantFirst, wantSecond, first, second);
            return false;
        }
    }
    return true;
}

// One word compared exactly: `word121` on 1.2.1, the recorded word on 1.4.3.
inline bool wordIs(std::uintptr_t base, Code site, std::uint32_t word121) {
    if (!supported() || !site.available()) return false;
    std::uint32_t want = word121;
    if (is143()) {
        const std::uint32_t* words = recordedWords(site.source());
        if (!words) return false;
        want = words[0];
    }
    return totk::suite::checkedCode(base + site) == want;
}

}  // namespace totk::port
