// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "totk/core/Units.hpp"
#include <array>
#include <limits>

namespace botw_abilities::wolf {

constexpr unsigned kCliffDirectionCount = 10;
using CliffDirections = std::array<totk::core::WorldPosition, kCliffDirectionCount>;

[[nodiscard]] CliffDirections cliffSearchDirections(totk::core::WorldPosition forward);

// Prefer the nearest completed corridor; otherwise retain the longest partial corridor.
class CliffEscapeChoice {
public:
    explicit CliffEscapeChoice(totk::core::WorldPosition firstEndpoint) : endpoint_(firstEndpoint) {}
    void consider(totk::core::WorldPosition start, totk::core::WorldPosition endpoint,
                  bool reached, totk::core::WorldPosition reference);
    [[nodiscard]] totk::core::WorldPosition endpoint() const { return endpoint_; }
private:
    totk::core::WorldPosition endpoint_;
    float nearest_ = std::numeric_limits<float>::max();
    float longest_ = -1.0f;
    bool reached_{};
};

}
