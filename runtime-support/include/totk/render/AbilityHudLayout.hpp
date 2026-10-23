// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <array>

namespace totk::render::ability_hud {
inline constexpr float centerX = 0.105f * 1280.0f;
inline constexpr float centerY = 0.17f * 720.0f;
inline constexpr float spacing = 50.0f, halfSize = 22.0f;
constexpr std::array<float,4> rect(unsigned slot) {
    return {centerX + spacing * slot,centerY,halfSize,halfSize};
}
} // namespace totk::render::ability_hud
