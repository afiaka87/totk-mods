#pragma once

#include <cstdint>

#include "totk/engine/Npad.hpp"

namespace self_recall::input {

constexpr std::uint64_t kButtonB = 1ull << 1;
constexpr std::uint64_t kButtonZL = 1ull << 8;
constexpr std::uint64_t kButtonDown = 1ull << 15;

constexpr std::uint8_t kHoldTicks = 45;

struct InputState {
    std::int32_t latestStickX = 0;
    std::int32_t latestStickY = 0;
};

totk::engine::NpadFrame read(void* device, InputState& state);

}  // namespace self_recall::input
