// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/engine/Pointer.hpp"
namespace stasis::equipment {
inline constexpr unsigned kDynamic = 65, kStatic = 146, kAttachment = 36;
inline constexpr unsigned kUser = 70;
struct Component {
    std::uintptr_t address{}, detail{};
    unsigned error{};
};
// ActorBase allocates count slots at +552 and stores count at +544 (1.2.1 E5905C).
template <class Read> Component find(std::uintptr_t actor, unsigned slot, Read read) {
    using namespace totk::engine;
    const int count = read.template operator()<int>(actor + 544);
    if (count < 0 || count > 256) return {0, unsigned(count), 1};
    if (slot >= unsigned(count)) return {};
    const auto table = read.template operator()<std::uintptr_t>(actor + 552);
    if (!table) return {};
    if (!isPlausibleAddress(table)) return {0, table, 2};
    const auto component = read.template operator()<std::uintptr_t>(table + 8 * slot);
    if (!component) return {};
    if (!isPlausibleAddress(component)) return {0, component, 3};
    const auto owner = read.template operator()<std::uintptr_t>(component + 24);
    if (owner != actor) return {0, owner, 4};
    return {component};
}
// EquipmentUser links store BaseProc IDs: eight dynamic (CAF228), twelve static (65040C).
template <class Read> bool contains(std::uintptr_t user, std::uint32_t id, Read read) {
    if (id == UINT32_MAX) return false;
    for (unsigned i = 0; i < 8; ++i)
        if (read.template operator()<std::uint32_t>(user + 48 + 24*i) == id) return true;
    for (unsigned i = 0; i < 12; ++i)
        if (read.template operator()<std::uint32_t>(user + 240 + 24*i) == id) return true;
    return false;
}
} // namespace stasis::equipment
