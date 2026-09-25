// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <lib.hpp>
#include "totk/render/PfxHookSite.hpp"

namespace zonai_hookshot::hooks {
inline constexpr std::uintptr_t kGripController = 0x024789bc;
inline constexpr std::uintptr_t kGripClimb = 0x01d56d50;
inline constexpr std::uint32_t kGripControllerWord = 0x6db923e9;
inline constexpr std::uint32_t kGripClimbWord = 0xa9be7bfd;
using ClimbCallback = std::uint64_t (*)(void*, void*, void*);

// Startup only. Keep the controller's pristine check; only the eight-byte
// aligned climb entry admits known detours, using the tested branch decoder.
template<class InstallInput>
bool installGripHooks(std::uintptr_t mainBase, ClimbCallback callback,
                      ClimbCallback& previous, InstallInput installInput) {
    if (previous) return true;
    const auto inputWord = *reinterpret_cast<const std::uint32_t*>(mainBase + kGripController);
    const auto site = mainBase + kGripClimb;
    const auto* words = reinterpret_cast<const std::uint32_t*>(site);
    const bool vanilla = words[0] == kGripClimbWord;
    const auto entry = totk::render::decodePfxEntry(site, words);
    const bool detour = entry.kind == totk::render::PfxEntryKind::Branch ||
                        entry.kind == totk::render::PfxEntryKind::Absolute;
    if (inputWord != kGripControllerWord || (!vanilla && !detour)) {
        Logging.Log("[glideshot] GRIP refused input=%08x climb=%08x,%08x",
                    inputWord, words[0], words[1]);
        return false;
    }
    if (!vanilla) {
        MemoryInfo info{};
        std::uint32_t page{};
        const auto query = svcQueryMemory(&info, &page, entry.previous);
        if (entry.previous == reinterpret_cast<std::uintptr_t>(callback) ||
            query || (info.perm & Perm_Rx) != Perm_Rx) {
            Logging.Log("[glideshot] GRIP refused target=%p query=%08x perm=%x",
                        reinterpret_cast<void*>(entry.previous), query, info.perm);
            return false;
        }
        previous = reinterpret_cast<ClimbCallback>(entry.previous);
        exl::hook::Hook(site, callback, false);
    } else {
        previous = exl::hook::Hook(site, callback, true);
    }
    // Install release detection before enabling any synthetic forward input.
    installInput();
    Logging.Log("[glideshot] GRIP ready=1 chained=%u previous=%p input=installed",
                unsigned(!vanilla), reinterpret_cast<void*>(previous));
    return true;
}
}
