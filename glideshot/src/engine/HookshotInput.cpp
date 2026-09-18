// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "HookshotInput.hpp"

namespace zonai_hookshot::input {
namespace {
// Sampling numbers persist across ticks; this is the mod's single reader.
totk::engine::NpadReader g_reader{};

namespace layout = totk::engine::layout;

constexpr std::ptrdiff_t kControllerLeftStick = 0x120;
constexpr std::ptrdiff_t kControllerNpadId = 0x178;
constexpr std::ptrdiff_t kControllerSamplingNumber = 0x180;

}  // namespace

totk::engine::NpadFrame readFrame(void* device) { return g_reader.read(device); }

LiveSlot newestLiveSlot(void* device) {
    LiveSlot live{};
    if (!device) return live;
    std::int64_t best = 0;  // require a positive sampling number
    for (std::size_t slot = 0; slot < layout::kNpadSlotCount; ++slot) {
        auto* state = static_cast<unsigned char*>(device) +
                      slot * static_cast<std::size_t>(layout::kNpadSlotStride) +
                      layout::kNpadState;
        const std::int64_t sample =
            *reinterpret_cast<const std::int64_t*>(state +
                                                   layout::kNpadSamplingNumber);
        if (sample > best) {
            best = sample;
            live.index = static_cast<int>(slot);
            live.state = state;
        }
    }
    live.samplingNumber = best;
    return live;
}

bool slotHolds(const LiveSlot& slot, std::uint64_t mask) {
    if (!slot.valid()) return false;
    return (*reinterpret_cast<const std::uint64_t*>(slot.state +
                                                    layout::kNpadButtons) &
            mask) != 0;
}

void pressInSlot(const LiveSlot& slot, std::uint64_t mask) {
    if (!slot.valid()) return;
    *reinterpret_cast<std::uint64_t*>(slot.state + layout::kNpadButtons) |= mask;
}

bool openProcessedController(void* controller, ProcessedController& out) {
    if (!controller) return false;
    auto* bytes = static_cast<unsigned char*>(controller);
    out.npadId = *reinterpret_cast<const std::uint32_t*>(bytes +
                                                         kControllerNpadId);
    out.samplingNumber = *reinterpret_cast<const std::uint64_t*>(
        bytes + kControllerSamplingNumber);
    out.leftStick = reinterpret_cast<float*>(bytes + kControllerLeftStick);
    return true;
}

}  // namespace zonai_hookshot::input
