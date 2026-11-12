// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <lib.hpp>

namespace botw_abilities::support {

// Validate every mapped region before borrowing native memory on hardware.
inline bool safeRead(std::uintptr_t address, void* output, std::size_t size) {
    constexpr auto limit = std::uintptr_t{1} << 39;
    if (address < 0x1000 || address >= limit || size > limit - address) return false;
    for (auto cursor = address; cursor < address + size;) {
        MemoryInfo region{};
        std::uint32_t page{};
        if (svcQueryMemory(&region, &page, cursor) || !(region.perm & 1) || !region.size ||
            cursor < region.addr || cursor - region.addr >= region.size) return false;
        const auto end = region.addr + region.size;
        if (end <= cursor) return false;
        cursor = end;
    }
    std::memcpy(output, reinterpret_cast<const void*>(address), size);
    return true;
}
}
