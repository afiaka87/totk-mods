// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>

namespace remote_bomb::engine::explosion {
struct DamageContact {
    std::uintptr_t actor{}, source{}, target{};
};
template <class Read, class Valid>
DamageContact chemicalDamageContact(std::uintptr_t info, Read read, Valid valid) {
    if (!valid(info))
        return {};
    return {read.template operator()<std::uintptr_t>(info),
            read.template operator()<std::uintptr_t>(info + 24),
            read.template operator()<std::uintptr_t>(info + 32)};
}
}
