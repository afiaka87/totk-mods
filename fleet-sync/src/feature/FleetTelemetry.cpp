#include "feature/FleetTelemetry.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <lib.hpp>
#include <nn/os.h>
#include "lib/program/loggers.hpp"

namespace linked_stick::feature {
namespace {
constexpr float kMilliScale = 1000.0f;
[[nodiscard]] std::int32_t scaledInteger(float value) {
    constexpr float kMinimum =
        static_cast<float>(std::numeric_limits<std::int32_t>::min() + 1);
    constexpr float kMaximum =
        static_cast<float>(std::numeric_limits<std::int32_t>::max());
    if (!std::isfinite(value)) return 0;
    const float scaled = value * kMilliScale;
    if (scaled <= kMinimum) return std::numeric_limits<std::int32_t>::min() + 1;
    if (scaled >= kMaximum) return std::numeric_limits<std::int32_t>::max();
    return static_cast<std::int32_t>(scaled);
}

}
void FleetTelemetry::enter() {
    tickCounter_.store(0, std::memory_order_release);
    resetPairState();
}
void FleetTelemetry::onPairPublished() { resetPairState(); }
void FleetTelemetry::onPairCleared() { resetPairState(); }
void FleetTelemetry::resetPairState() {
    for (auto& endpoint : inputs_) {
        for (auto& value : endpoint.values) {
            value.store(std::bit_cast<std::uint32_t>(0.0f), std::memory_order_relaxed);
        }
        for (auto& value : endpoint.calls) value.store(0, std::memory_order_relaxed);
        for (auto& value : endpoint.ticks) value.store(0, std::memory_order_relaxed);
    }
}

void FleetTelemetry::recordInput(std::size_t endpointIndex,
                                 ControlAxis axis, float value) {
    if (endpointIndex >= kEndpointCount || !std::isfinite(value)) return;
    const std::size_t axisIndex = static_cast<std::size_t>(axis);
    inputs_[endpointIndex].values[axisIndex].store(
        std::bit_cast<std::uint32_t>(value), std::memory_order_relaxed);
    inputs_[endpointIndex].calls[axisIndex].fetch_add(1, std::memory_order_relaxed);
    inputs_[endpointIndex].ticks[axisIndex].store(currentTick(), std::memory_order_relaxed);
}

void FleetTelemetry::tick(const FleetTelemetryEndpoints& endpoints) {
    const std::uint64_t tick =
        tickCounter_.fetch_add(1, std::memory_order_acq_rel) + 1;
    const auto now = svcGetSystemTick();
    const auto frequency = nn::os::GetSystemTickFrequency();
    if (!axesAt_ || (frequency && now - axesAt_ >= frequency)) {
        axesAt_ = now;
        const auto count = std::min<std::uint32_t>(endpoints.receiverCount, 4);
        for (unsigned i = 0; i <= count; ++i) {
            const auto& a = inputs_[i];
            Logging.Log("[fleet-sync][input] AXES endpoint=%u active=%u tick=%llu "
                        "x_fb_y_m=%d,%d,%d calls=%u,%u,%u last_ticks=%llu,%llu,%llu",
                        i, endpoints.controllerActive ? 1u : 0u, static_cast<unsigned long long>(tick),
                        scaledInteger(std::bit_cast<float>(a.values[0].load())),
                        scaledInteger(std::bit_cast<float>(a.values[1].load())),
                        scaledInteger(std::bit_cast<float>(a.values[2].load())),
                        a.calls[0].load(), a.calls[1].load(), a.calls[2].load(),
                        static_cast<unsigned long long>(a.ticks[0].load()),
                        static_cast<unsigned long long>(a.ticks[1].load()),
                        static_cast<unsigned long long>(a.ticks[2].load()));
        }
    }

}
}
