// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstddef>
#include <cstdint>

namespace totk::core {
struct SceneToken {
    std::uintptr_t value = 0;

    [[nodiscard]] constexpr bool isValid() const { return value != 0; }
    [[nodiscard]] friend constexpr bool operator==(SceneToken, SceneToken) = default;
};

struct SceneGeneration {
    std::uint32_t value = 0;

    [[nodiscard]] friend constexpr bool operator==(SceneGeneration, SceneGeneration) = default;
};

struct ImageOffset {
    std::ptrdiff_t value = 0;
};

struct WorldPosition {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

}
