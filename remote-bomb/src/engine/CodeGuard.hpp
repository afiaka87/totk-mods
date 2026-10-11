// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/engine/Pointer.hpp"
#include <lib.hpp>
#include <cstdint>
#include <span>

namespace remote_bomb::engine {
// The first two instructions of a TotK 1.2.1 function the mod hooks, calls or reads.
struct CodeGuard {
    std::uintptr_t offset;
    std::uint32_t first, second;
};
// True when every guard matches the running game; the first mismatch is logged under tag.
inline bool codeMatches(std::uintptr_t base, std::span<const CodeGuard> guards, const char *tag) {
    for (const auto guard : guards) {
        const auto first = totk::engine::readMemory<std::uint32_t>(base + guard.offset);
        const auto second = totk::engine::readMemory<std::uint32_t>(base + guard.offset + 4);
        if (first != guard.first || second != guard.second) {
            Logging.Log("[remote-bomb] %s offset=%p expected=%08x,%08x actual=%08x,%08x", tag,
                        reinterpret_cast<void *>(guard.offset), guard.first, guard.second, first, second);
            return false;
        }
    }
    return true;
}
}
