// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#include "HookshotInput.hpp"

namespace arrowbound::input {
namespace {
// Sampling numbers persist across ticks; this is the mod's single reader.
totk::engine::NpadReader g_reader{};

}  // namespace

totk::engine::NpadFrame readFrame(void* device) { return g_reader.read(device); }

int newestLiveController(void* device) {
    if (!device) return -1;
    namespace layout = totk::engine::layout;
    std::int64_t best = 0;
    int result = -1;
    for (std::size_t slot = 0; slot < layout::kNpadSlotCount; ++slot) {
        const auto* state = static_cast<const unsigned char*>(device) +
            slot * static_cast<std::size_t>(layout::kNpadSlotStride) + layout::kNpadState;
        const auto sample = *reinterpret_cast<const std::int64_t*>(state + layout::kNpadSamplingNumber);
        if (sample > best) {
            best = sample;
            result = static_cast<int>(slot);
        }
    }
    return result;
}

bool openProcessedController(void* controller, ProcessedController& out) {
    if (!controller) return false;
    auto* bytes = static_cast<unsigned char*>(controller);
    // Identical to the accepted Zonai Hookshot 1.2.1 controller adapter.
    out.npadId = *reinterpret_cast<const std::uint32_t*>(bytes + 0x178);
    out.samplingNumber = *reinterpret_cast<const std::uint64_t*>(bytes + 0x180);
    out.leftStick = reinterpret_cast<float*>(bytes + 0x120);
    return true;
}

}  // namespace arrowbound::input
