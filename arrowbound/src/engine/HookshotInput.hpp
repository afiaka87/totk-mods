// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Controller seams: the gameplay frame (fresh samples, owned mask), the newest live raw slot, and
// the processed controller after calcImpl_.
#pragma once

#include "EngineNamespace.hpp"

#include <cstdint>

#include "totk/engine/Npad.hpp"

namespace HOOKSHOT_ENGINE_NS::input {
constexpr std::uint64_t kButtonA = 1ull << 0;
constexpr std::uint64_t kButtonB = 1ull << 1;
constexpr std::uint64_t kButtonX = 1ull << 2;
constexpr std::uint64_t kButtonZR = 1ull << 9;

totk::engine::NpadFrame readFrame(void* device);

// Newest live raw slot: the highest positive sampling number.
struct LiveSlot {
    int index = -1;
    std::int64_t samplingNumber = 0;
    unsigned char* state = nullptr;

    [[nodiscard]] bool valid() const { return index >= 0 && state != nullptr; }
};
LiveSlot newestLiveSlot(void* device);
inline int newestLiveController(void* device) { return newestLiveSlot(device).index; }
bool slotHolds(const LiveSlot& slot, std::uint64_t mask);
void pressInSlot(const LiveSlot& slot, std::uint64_t mask);

struct ProcessedController {
    std::uint32_t npadId = 0;
    std::uint64_t samplingNumber = 0;
    float* leftStick = nullptr;
};
bool openProcessedController(void* controller, ProcessedController& out);

}  // namespace HOOKSHOT_ENGINE_NS::input
