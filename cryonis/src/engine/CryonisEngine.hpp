// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/engine/Pointer.hpp"
namespace cryonis::engine {
extern std::uintptr_t moduleBase;
bool heapPointer(std::uintptr_t address);
template <class T> T read(std::uintptr_t address) { return totk::engine::readMemory<T>(address); }
template <class F> F call(std::uintptr_t offset) {
    return reinterpret_cast<F>(moduleBase + offset);
}
inline bool valid(std::uintptr_t address) { return totk::engine::isPlausibleAddress(address); }
}
