
#pragma once

#include "totk/engine/Runtime.hpp"

#include <cstddef>
#include <cstdint>

namespace totk::engine {

struct InputSnapshot {
    std::uint64_t buttons = 0;
};

class NpadFrame {
public:
    [[nodiscard]] const InputSnapshot& snapshot() const { return snapshot_; }

    void maskOwnedButtons(std::uint64_t ownedMask) const {
        for (std::uint8_t index = 0; index < sampleCount_; ++index) {
            const auto buttonsAddress =
                sampleStates_[index] + layout::kNpadButtons;
            const auto buttons = readMemory<std::uint64_t>(buttonsAddress);
            writeMemory(buttonsAddress, buttons & ~ownedMask);
        }
    }

    void writeOwnedLeftStick(std::int32_t x, std::int32_t y) const {
        for (std::uint8_t index = 0; index < sampleCount_; ++index) {
            writeMemory(sampleStates_[index] + layout::kNpadLeftStickX, x);
            writeMemory(sampleStates_[index] + layout::kNpadLeftStickY, y);
        }
    }

private:
    friend class NpadReader;
    InputSnapshot snapshot_{};
    std::uintptr_t sampleStates_[layout::kNpadSlotCount]{};
    std::uint8_t sampleCount_ = 0;
};

class NpadReader {
public:
    [[nodiscard]] NpadFrame read(void* device) {
        NpadFrame frame{};
        if (!device) return frame;

        const auto base = reinterpret_cast<std::uintptr_t>(device);
        for (std::size_t slot = 0; slot < layout::kNpadSlotCount; ++slot) {
            const auto state =
                base + slot * static_cast<std::uintptr_t>(layout::kNpadSlotStride) +
                layout::kNpadState;
            const auto samplingNumber =
                readMemory<std::int64_t>(state + layout::kNpadSamplingNumber);
            if (samplingNumber == samplingNumbers_[slot]) continue;
            samplingNumbers_[slot] = samplingNumber;

            frame.snapshot_.buttons |=
                readMemory<std::uint64_t>(state + layout::kNpadButtons);
            frame.sampleStates_[frame.sampleCount_++] = state;
        }
        return frame;
    }

private:
    std::int64_t samplingNumbers_[layout::kNpadSlotCount]{};
};

}
