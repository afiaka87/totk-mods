#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace linked_stick::feature {

enum class ControlAxis : std::uint8_t {
    X,
    ForwardBack,
    Y,
};

struct FleetTelemetryEndpoints {
    static constexpr std::size_t kMaximumReceivers = 4;

    std::uintptr_t controllerReceiver = 0;
    std::array<std::uintptr_t, kMaximumReceivers> receiverComponents{};
    std::uint32_t receiverCount = 0;
    bool controllerActive = false;
};

class FleetTelemetry {
public:
    static constexpr std::size_t kEndpointCount = 5;

    void enter();
    void onPairPublished();
    void onPairCleared();
    void tick(const FleetTelemetryEndpoints& endpoints);

    void recordInput(std::size_t endpointIndex, ControlAxis axis,
                     float value);

    [[nodiscard]] std::uint64_t currentTick() const {
        return tickCounter_.load(std::memory_order_acquire);
    }

private:
    struct AtomicInput {
        std::array<std::atomic<std::uint32_t>, 3> values{};
        std::array<std::atomic<std::uint32_t>, 3> calls{};
        std::array<std::atomic<std::uint64_t>, 3> ticks{};
    };
    std::uint64_t axesAt_ = 0;

    void resetPairState();

    std::array<AtomicInput, kEndpointCount> inputs_{};
    std::atomic<std::uint64_t> tickCounter_{0};
};

}
