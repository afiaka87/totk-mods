// SPDX-License-Identifier: MIT
// Host substitutes for the PFX installer's only platform dependencies.
#pragma once
#include <cstdint>
#include <cstring>
struct MemoryInfo { unsigned perm{}; };
inline constexpr unsigned Perm_Rx = 5;
inline unsigned queryResult{}, queryPermission = Perm_Rx;
inline unsigned svcQueryMemory(MemoryInfo* info, std::uint32_t*, std::uintptr_t) {
    info->perm = queryPermission;
    return queryResult;
}
struct TestLogger { template<class... Args> void Log(const char*, Args...) {} };
inline TestLogger Logging;
namespace exl::hook {
inline std::uintptr_t original{};
inline unsigned trampolineCount{}, patchCount{};
template<class Callback>
Callback Hook(std::uintptr_t site, Callback callback, bool trampoline) {
    auto* words = reinterpret_cast<std::uint32_t*>(site);
    std::uintptr_t predecessor = original;
    if (words[0] == 0x58000051 && words[1] == 0xd61f0220)
        std::memcpy(&predecessor, words + 2, sizeof(predecessor));
    words[0] = 0x58000051;
    words[1] = 0xd61f0220;
    const auto target = reinterpret_cast<std::uintptr_t>(callback);
    std::memcpy(words + 2, &target, sizeof(target));
    ++patchCount;
    if (trampoline) ++trampolineCount;
    return trampoline ? reinterpret_cast<Callback>(predecessor) : nullptr;
}
}
