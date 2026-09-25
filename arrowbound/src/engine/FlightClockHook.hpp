// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <lib.hpp>
#include "../../../runtime-support/include/totk/render/PfxHookSite.hpp"

namespace arrowbound::game_clock {
using FrameCallback = void (*)(void*, const void*, float);
inline bool installClockHook(std::uintptr_t site, FrameCallback callback, FrameCallback& previous) {
    if (previous) return true;
    const auto* words = reinterpret_cast<const std::uint32_t*>(site);
    if (words[0] == 0xf9400828) {
        previous = exl::hook::Hook(site, callback, true);
        Logging.Log("[arrowbound] FLIGHT_CLOCK_READY chained=0");
        return true;
    }
    const auto entry = totk::render::decodePfxEntry(site, words);
    if (entry.kind != totk::render::PfxEntryKind::Branch &&
        entry.kind != totk::render::PfxEntryKind::Absolute) {
        Logging.Log("[arrowbound] FLIGHT_CLOCK_REFUSED words=%08x,%08x", words[0],words[1]);
        return false;
    }
    MemoryInfo info{};
    std::uint32_t page{};
    const auto query=svcQueryMemory(&info,&page,entry.previous);
    if (query || (info.perm & Perm_Rx)!=Perm_Rx ||
        entry.previous==reinterpret_cast<std::uintptr_t>(callback)) {
        Logging.Log("[arrowbound] FLIGHT_CLOCK_REFUSED target=%p query=%x perm=%x",
                    reinterpret_cast<void*>(entry.previous),query,info.perm);
        return false;
    }
    previous=reinterpret_cast<FrameCallback>(entry.previous);
    exl::hook::Hook(site,callback,false);
    Logging.Log("[arrowbound] FLIGHT_CLOCK_READY chained=1 previous=%p",reinterpret_cast<void*>(previous));
    return true;
}
}
