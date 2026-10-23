// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <array>
#include <atomic>

namespace totk::render {
inline constexpr unsigned kExtraHudIcons = 4;
struct HudTextureSource {
    const unsigned char* data = nullptr;
    unsigned bytes = 0, width = 0, height = 0, format = 0;
    unsigned char swizzle[4]{};
};
struct HudItem {
    std::array<float,4> rect{}, tint{1,1,1,1};
    float progress = 1;
    unsigned icon = 0, charges = 0, capacity = 0;
    bool recharging = false;
};
struct HudFrame { std::array<HudItem,kExtraHudIcons> items{}; unsigned count = 0; };
struct HudExtension {
    std::array<HudTextureSource,kExtraHudIcons> textures{};
    bool (*read)(HudFrame&) = nullptr;
};
inline std::atomic<const HudExtension*> g_hudExtension{};
inline bool registerHudExtension(const HudExtension& extension) {
    if (!extension.read) return false;
    for (const auto& texture : extension.textures)
        if (!texture.data || !texture.bytes || !texture.width || !texture.height) return false;
    const HudExtension* expected = nullptr;
    return g_hudExtension.compare_exchange_strong(expected,&extension,std::memory_order_release,std::memory_order_relaxed);
}
inline const HudExtension* hudExtension() { return g_hudExtension.load(std::memory_order_acquire); }
} // namespace totk::render
