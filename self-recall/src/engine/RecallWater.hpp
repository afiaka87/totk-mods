#pragma once
#include "RecallActorModelView.hpp"
#include <cmath>

namespace self_recall::water {
inline bool surfaceHeight(const void* player, float& height) {
    using actor_model::read;
    const auto* registry = player ? actor_model::registry(player) : nullptr;
    const auto* physics = registry ? read<const void*>(registry, 0x50) : nullptr;
    const auto* controllers = physics ? read<const void*>(physics, 0x20) : nullptr;
    const auto* cct = controllers ? read<const void*>(controllers, 0x10) : nullptr;
    const auto* updates = cct ? read<const void*>(cct, 8) : nullptr;
    const auto* floating = updates ? read<const void*>(updates, 0x30) : nullptr;
    if (!floating || !(read<std::uint8_t>(floating, 0x19C) & 1) ||
        !read<std::uint8_t>(floating, 0x188)) return false;
    height = read<float>(floating, 0xE4);
    return std::isfinite(height);
}
}
