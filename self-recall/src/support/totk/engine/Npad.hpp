
#pragma once

#include "totk/engine/Pointer.hpp"
#include "totk/engine/Totk121Offsets.hpp"

#include <cstddef>
#include <cstdint>

namespace totk::engine {

struct StickPosition {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

struct InputSnapshot {
    std::uint64_t buttons = 0;
    StickPosition leftStick{};
    StickPosition rightStick{};
    std::uint8_t freshSampleCount = 0;

    [[nodiscard]] bool held(std::uint64_t mask) const {
        return (buttons & mask) == mask;
    }
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

    void writeOwnedRightStick(std::int32_t x, std::int32_t y) const {
        for (std::uint8_t index = 0; index < sampleCount_; ++index) {
            writeMemory(sampleStates_[index] + layout::kNpadRightStickX, x);
            writeMemory(sampleStates_[index] + layout::kNpadRightStickY, y);
        }
    }

    void addOwnedButtons(std::uint64_t ownedMask) const {
        for (std::uint8_t index = 0; index < sampleCount_; ++index) {
            const auto buttonsAddress =
                sampleStates_[index] + layout::kNpadButtons;
            const auto buttons = readMemory<std::uint64_t>(buttonsAddress);
            writeMemory(buttonsAddress, buttons | ownedMask);
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
        std::int64_t strongestMagnitude = -1;
        std::int64_t strongestRightMagnitude = -1;
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

            const auto x =
                readMemory<std::int32_t>(state + layout::kNpadLeftStickX);
            const auto y =
                readMemory<std::int32_t>(state + layout::kNpadLeftStickY);
            const auto magnitude =
                absolute(x) + absolute(y);
            if (magnitude > strongestMagnitude) {
                strongestMagnitude = magnitude;
                frame.snapshot_.leftStick = StickPosition{x, y};
            }

            const auto rightX =
                readMemory<std::int32_t>(state + layout::kNpadRightStickX);
            const auto rightY =
                readMemory<std::int32_t>(state + layout::kNpadRightStickY);
            const auto rightMagnitude = absolute(rightX) + absolute(rightY);
            if (rightMagnitude > strongestRightMagnitude) {
                strongestRightMagnitude = rightMagnitude;
                frame.snapshot_.rightStick = StickPosition{rightX, rightY};
            }
        }
        frame.snapshot_.freshSampleCount = frame.sampleCount_;
        return frame;
    }

    void reset() {
        for (auto& samplingNumber : samplingNumbers_) samplingNumber = 0;
    }

private:
    [[nodiscard]] static constexpr std::int64_t absolute(std::int32_t value) {
        return value < 0 ? -static_cast<std::int64_t>(value)
                         : static_cast<std::int64_t>(value);
    }

    std::int64_t samplingNumbers_[layout::kNpadSlotCount]{};
};

} // namespace totk::engine
