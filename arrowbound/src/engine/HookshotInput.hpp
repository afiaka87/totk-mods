// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Gameplay frame ownership plus the proven processed-stick lane for terminal wall grip.
#pragma once

#include <cstdint>

#include "totk/engine/Npad.hpp"

namespace arrowbound::input {
constexpr std::uint64_t kButtonB = 1ull << 1;
constexpr std::uint64_t kButtonZR = 1ull << 9;  // nn::hid::NpadButton::ZR

totk::engine::NpadFrame readFrame(void* device);

int newestLiveController(void* device);

struct ProcessedController {
    std::uint32_t npadId = 0;
    std::uint64_t samplingNumber = 0;
    float* leftStick = nullptr;
};
bool openProcessedController(void* controller, ProcessedController& out);

}  // namespace arrowbound::input
