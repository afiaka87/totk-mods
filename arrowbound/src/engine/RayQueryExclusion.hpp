// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include "EngineNamespace.hpp"

#include <cstdint>
#include <cstring>

namespace HOOKSHOT_ENGINE_NS::engine {

struct RayGroup {
    std::uint32_t id = 0;
    std::uint8_t enabled = 0;
    std::uint8_t padding[3]{};
};
static_assert(sizeof(RayGroup) == 8);

struct NativeRayGroup : RayGroup {
    ~NativeRayGroup() {} // Native value has no retained pointer or release side effect.
};
static_assert(sizeof(NativeRayGroup) == 8);

// Rebase both filter pointers into the copied query, never the live donor.
inline bool rebaseRayFilter(void* query, const void* donor) {
    auto* bytes = static_cast<unsigned char*>(query);
    std::uintptr_t source = 0;
    std::memcpy(&source, bytes + 96, sizeof(source));
    if (source != reinterpret_cast<std::uintptr_t>(donor) + 0x128) return false;
    const auto own = reinterpret_cast<std::uintptr_t>(query) + 0x128;
    std::memcpy(bytes + 96, &own, sizeof(own));
    return true;
}

} // namespace HOOKSHOT_ENGINE_NS::engine
