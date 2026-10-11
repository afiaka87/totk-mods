// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "NativeInventoryPolicy.hpp"
#include <lib.hpp>

namespace remote_bomb::inventory {
inline std::uintptr_t managerAt(std::uintptr_t slot) {
    const auto holder = totk::engine::readMemory<std::uintptr_t>(slot);
    if (!totk::engine::isPlausibleAddress(holder))
        return 0;
    const auto manager = totk::engine::readMemory<std::uintptr_t>(holder);
    return totk::engine::isPlausibleAddress(manager) ? manager : 0;
}
inline bool report(unsigned &attempts) {
    ++attempts;
    return attempts <= 4 || (attempts & (attempts - 1)) == 0;
}
inline Status deferred(const char *step, Status status, std::uintptr_t detail = 0) {
    static unsigned attempts{};
    if (report(attempts))
        Logging.Log("[remote-bomb] INVENTORY_WAIT step=%s status=%u detail=%llx attempt=%u",
                    step, unsigned(status), static_cast<unsigned long long>(detail), attempts);
    return status;
}
// Failed name-read step: index, slot record, native reader, pointer, terminator.
enum class NameStage : unsigned { None, Index, Slot, Read, Pointer, Terminator };
struct Reader {
    std::uintptr_t base, manager;
    unsigned count;
    std::uintptr_t handle;
    Reader(std::uintptr_t b, std::uintptr_t p) : base(b), manager(managerAt(b + 0x462E3D8)),
        count(totk::engine::isPlausibleAddress(p) ?
            reinterpret_cast<unsigned (*)(std::uintptr_t, unsigned)>(b + 0xD44A08)(p, kMaterialCategory) : ~0u),
        handle(totk::engine::isPlausibleAddress(p) ?
            reinterpret_cast<std::uintptr_t (*)(std::uintptr_t, unsigned)>(b + 0xD4499C)(p, kMaterialCategory) : 0) {}
    bool ready() const {
        return manager && count <= kMaximumItems && totk::engine::isPlausibleAddress(handle);
    }
    NameStage name(unsigned i, const char *&text, std::uint64_t (&record)[2], bool queued) const {
        using Slot = unsigned (*)(std::uintptr_t, void *, std::uintptr_t, unsigned, unsigned);
        using Name = unsigned (*)(std::uintptr_t, const char **, const void *, unsigned);
        // Native callers seed both readers with the shared empty string (1A768B4, CDF750).
        text = totk::engine::readMemory<const char *>(base + 0x462E1B8);
        if (i >= count)
            return NameStage::Index;
        if (!(reinterpret_cast<Slot>(base + 0x942FE8)(manager, record, handle, 0x4290322E, i) & 1))
            return NameStage::Slot;
        if (!(reinterpret_cast<Name>(base + (queued ? 0x935BE0 : 0xD44600))(
                manager, &text, record, 0x25EFA387) & 1))
            return NameStage::Read;
        if (!validName(text))
            return NameStage::Pointer;
        return std::memchr(text, 0, 64) ? NameStage::None : NameStage::Terminator;
    }
    NameStage classify(unsigned i, unsigned &rank, bool queued) const {
        alignas(8) std::uint64_t record[2]{};
        const char *text{};
        const auto stage = name(i, text, record, queued);
        if (stage == NameStage::None)
            rank = priority(text);
        return stage;
    }
};
}
