// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "BombReference.hpp"
#include <cstdint>

namespace remote_bomb::inventory {
enum class Status { Unavailable, Deferred, Missing, Present, Granted, Rejected };
struct Stock {
    Status status{};
    int slot = -1, quantity{};
    unsigned failed{}, stage{};
};
Status ensure(std::uintptr_t base, Shape shape, Upgrade upgrade, bool workerAdvanced);
// Reads Materials stock without writing; committed selects the reader used by native sorting.
Stock probe(std::uintptr_t base, Shape shape, Upgrade upgrade, bool committed);
void prioritizePouch(std::uintptr_t base, std::uintptr_t pouch, unsigned category, std::uintptr_t list);
void prioritizeShortcut(std::uintptr_t base, std::uintptr_t pouch);
}
