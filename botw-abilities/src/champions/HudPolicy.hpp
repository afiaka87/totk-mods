// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "SavedState.hpp"
#include "totk/render/AbilityHudLayout.hpp"
#include "totk/render/HudExtension.hpp"
#include <algorithm>

namespace botw_abilities::champions::hud {
inline constexpr std::array<std::array<float,4>,4> kColors{{
    {0.28f,0.78f,1,1},{0.38f,1,0.72f,1},{1,0.3f,0.08f,1},{1,0.78f,0.18f,1}}};
inline totk::render::HudFrame makeFrame(const States& states, const std::array<Context,4>& contexts) {
    totk::render::HudFrame frame;
    for (unsigned i = 0; i < states.size(); ++i) {
        const auto& state = states[i];
        const bool cooling = state.remainingSeconds > 0;
        if (!state.enabled || !contexts[i].gameplay || contexts[i].shrine) continue;
        auto& item = frame.items[frame.count++];
        item.rect = totk::render::ability_hud::rect(i+2);
        item.rect[1] -= 8.0f;
        item.tint = kColors[i];
        item.progress = cooling ? std::clamp(1.0f-state.remainingSeconds/rule(static_cast<Blessing>(i)).rechargeSeconds,0.0f,1.0f) : 1.0f;
        item.icon = i; item.charges = state.charges;
        item.capacity = rule(static_cast<Blessing>(i)).charges;
        item.recharging = cooling;
    }
    return frame;
}
} // namespace botw_abilities::champions::hud
