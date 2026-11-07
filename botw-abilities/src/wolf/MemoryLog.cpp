// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "MemoryLog.hpp"
#include "support/SafeRead.hpp"
#include <lib.hpp>

namespace botw_abilities::wolf {
namespace {
using support::safeRead;
constexpr std::uintptr_t kSystemSlot = 0x462EEB8, kActorHeap = 8;
std::uintptr_t g_base{};
}

void initializeMemoryLog(std::uintptr_t base) { g_base = base; }

void logResourceMemory(const char* moment) {
    std::uintptr_t slot{}, system{};
    std::array<PoolUse, 16> pools;
    unsigned count = 0;
    if (g_base && safeRead(g_base + kSystemSlot, &slot, sizeof(slot)) && safeRead(slot, &system, sizeof(system)))
        count = inspectResourcePools(safeRead, system, pools);
    if (!count) Logging.Log("[wolf-link] MEMORY_UNAVAILABLE moment=%s", moment);
    for (unsigned i = 0; i < count; ++i)
        Logging.Log("[wolf-link] MEMORY moment=%s pool=%s total=%u active=%u units=%d flags=%x", moment,
            pools[i].name.data(), pools[i].total, pools[i].active, pools[i].units, pools[i].flags);
}

void logActorHeap(const ActorReference& actor, const char* moment) {
    std::uintptr_t heap{};
    if (!actor.address() || !safeRead(actor.address() + kActorHeap, &heap, sizeof(heap))) {
        Logging.Log("[wolf-link] MEMORY_HEAP_UNAVAILABLE moment=%s", moment);
        return;
    }
    const auto use = inspectActorHeap(safeRead, heap);
    Logging.Log("[wolf-link] MEMORY_HEAP moment=%s used=%llu size=%llu spare=%llu spare-blocks=%u free=%llu free-blocks=%u complete=%u",
        moment, static_cast<unsigned long long>(use.used()), static_cast<unsigned long long>(use.size),
        static_cast<unsigned long long>(use.spare), use.spareBlocks, static_cast<unsigned long long>(use.free),
        use.freeBlocks, unsigned(use.complete));
    // The computed line above has read garbage on the real heap, so also dump the raw sead layout. Walk the
    // heap's ancestor chain (the actor's instance heap may be a parent of the one at +8, whose reserved size
    // does not match the donor's 491,984 budget) and log each heap's name (ptr at +0x20), reserved size
    // (+0x30), child count (+0x50) and the raw words over the ExpHeap free/use-list region (~0xA0). From one
    // boot this resolves the true free-list offset and the used size offline instead of guessing here.
    // Read-only and bounded, so it is safe from any thread.
    std::uintptr_t node = heap;
    for (unsigned depth = 0; depth < 8 && node >= 0x1000; ++depth) {
        std::uint64_t size = 0;
        std::uintptr_t parent = 0, namePtr = 0;
        std::int32_t children = 0;
        char name[24] = {};
        safeRead(node + 0x30, &size, sizeof(size));
        safeRead(node + 0x38, &parent, sizeof(parent));
        safeRead(node + 0x50, &children, sizeof(children));
        if (safeRead(node + 0x20, &namePtr, sizeof(namePtr)) && namePtr >= 0x1000)
            for (int i = 0; i < 23; ++i) {
                char letter{};
                if (!safeRead(namePtr + std::uintptr_t(i), &letter, 1) || letter < 32 || letter > 126) break;
                name[i] = letter;
            }
        std::uint64_t w[12] = {};
        for (int i = 0; i < 12; ++i) safeRead(node + 0xA0 + std::uintptr_t(i) * 8, &w[i], sizeof(std::uint64_t));
        Logging.Log("[wolf-link] HEAP moment=%s depth=%u addr=%llx name=%s size=%llu children=%d parent=%llx",
            moment, depth, static_cast<unsigned long long>(node), name, static_cast<unsigned long long>(size),
            children, static_cast<unsigned long long>(parent));
        Logging.Log("[wolf-link] HEAPW moment=%s depth=%u w=%llx,%llx,%llx,%llx,%llx,%llx,%llx,%llx,%llx,%llx,%llx,%llx",
            moment, depth, static_cast<unsigned long long>(w[0]), static_cast<unsigned long long>(w[1]),
            static_cast<unsigned long long>(w[2]), static_cast<unsigned long long>(w[3]),
            static_cast<unsigned long long>(w[4]), static_cast<unsigned long long>(w[5]),
            static_cast<unsigned long long>(w[6]), static_cast<unsigned long long>(w[7]),
            static_cast<unsigned long long>(w[8]), static_cast<unsigned long long>(w[9]),
            static_cast<unsigned long long>(w[10]), static_cast<unsigned long long>(w[11]));
        if (parent == node || parent < 0x1000) break;
        node = parent;
    }
}
}
