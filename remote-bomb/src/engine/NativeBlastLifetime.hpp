// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>

namespace remote_bomb::engine::explosion {
template <class Read, class Valid>
bool calculatingBombOwner(std::uintptr_t actor, Read read, Valid valid) {
    return valid(actor) && read.template operator()<unsigned>(actor + 36) == 4;
}
template <class Read, class Valid>
std::uintptr_t explosionElementList(std::uintptr_t object, Read read, Valid valid) {
    if (!valid(object) || read.template operator()<unsigned>(object + 704) <= 1)
        return 0;
    const auto lists = read.template operator()<std::uintptr_t>(object + 712);
    return valid(lists) ? lists + 32 : 0;
}
template <class Read, class Valid>
std::uintptr_t deathProcedureActor(std::uintptr_t component, Read read, Valid valid) {
    return valid(component) ? read.template operator()<std::uintptr_t>(component + 24) : 0;
}
}
