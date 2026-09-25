// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Three controller seams: the gameplay frame (fresh samples, owned mask), the newest live raw slot
// (launch injector), and the processed controller after calcImpl_.
#pragma once

#include <cstdint>

#include "totk/engine/Npad.hpp"
#include "../pure/ActivationButtons.hpp"

namespace zonai_hookshot::input {
constexpr std::uint64_t kButtonA = 1ull << 0;
constexpr std::uint64_t kButtonB = 1ull << 1;
constexpr std::uint64_t kButtonX = 1ull << 2;  // the synthetic launch press
constexpr std::uint64_t kButtonL = input_policy::kButtonL;
constexpr std::uint64_t kButtonZL = input_policy::kButtonZL;
constexpr std::uint64_t kAimChord = input_policy::kAimChord;
static_assert((kAimChord & ((1ull<<4)|(1ull<<5))) == 0);

totk::engine::NpadFrame readFrame(void* device);

// Newest live raw slot: highest positive sampling number; used only to pick the controller and
// place the synthetic press.
struct LiveSlot {
    int index = -1;
    std::int64_t samplingNumber = 0;
    unsigned char* state = nullptr;

    [[nodiscard]] bool valid() const { return index >= 0 && state != nullptr; }
};
LiveSlot newestLiveSlot(void* device);
bool slotHolds(const LiveSlot& slot, std::uint64_t mask);
void pressInSlot(const LiveSlot& slot, std::uint64_t mask);

// Processed controller layout (1.2.1): mLeftStick +0x120, NinJoyNpadController id +0x178, sampling
// number +0x180.
struct ProcessedController {
    std::uint32_t npadId = 0;
    std::uint64_t samplingNumber = 0;
    float* leftStick = nullptr;
};
bool openProcessedController(void* controller, ProcessedController& out);

}  // namespace zonai_hookshot::input
