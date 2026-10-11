// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "../pure/BombInput.hpp"
#include <cstdint>

namespace remote_bomb::engine {
struct KeyFilter { unsigned changed{}, invalid{}; };

// CalcKey stores held/pressed words through node+40/+56 directly (1.2.1).
template <class Read, class Write, class Valid>
KeyFilter filterAbilityKeys(std::uintptr_t node, bool owned, Read read, Write write, Valid valid) {
    KeyFilter result;
    if (!owned)
        return result;
    for (unsigned i = 0; i < 2; ++i) {
        const auto address = std::uintptr_t(read(node + 40 + i * 16));
        if (!valid(address)) {
            result.invalid |= 1u << i;
            continue;
        }
        const auto before = read(address), after = input::gameplayKeys(before, true);
        if (after != before) {
            write(address, after);
            result.changed |= 1u << i;
        }
    }
    return result;
}
}
