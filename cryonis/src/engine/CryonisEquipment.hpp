// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "Totk121Layout.hpp"
#include <cstdint>
#include <initializer_list>

namespace cryonis::engine {
template <class Read, class Valid>
std::uintptr_t physicalBody(std::uintptr_t actor, Read read, Valid valid) {
    auto pointer = actor;
    for (const auto offset : {layout::actor::kComponents, layout::actor::kPhysics,
                              layout::actor::kControllerSet, std::size_t{0x150}}) {
        if (!valid(pointer) || !valid(pointer + offset))
            return 0;
        pointer = read(pointer + offset);
    }
    return valid(pointer) ? pointer : 0;
}

template <class Read, class Valid>
bool actorName(std::uintptr_t pointer, Read read, Valid valid) {
    for (unsigned i = 0; i < 64; ++i) {
        if (!valid(pointer + i))
            return false;
        const auto c = read(pointer + i);
        if (!c)
            return i != 0;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return false;
}

template <class Read, class Valid>
std::uintptr_t equipmentComponent(std::uintptr_t actor, Read read, Valid valid) {
    if (!valid(actor))
        return 0;
    const auto registryAddress = actor + layout::actor::kComponents;
    if (!valid(registryAddress))
        return 0;
    const auto components = read(registryAddress);
    if (!valid(components))
        return 0;
    const auto slotAddress = components + layout::actor::kDynamicEquipment;
    if (!valid(slotAddress))
        return 0;
    const auto equipment = read(slotAddress);
    return valid(equipment) ? equipment : 0;
}
}
