#pragma once

#include <cstdint>

#include "RecallRouteProbePlan.hpp"
#include "totk/core/Units.hpp"
#include "totk/engine/Raycast.hpp"

namespace self_recall::probe {

constexpr int kTimeoutTicks = 24;
constexpr std::uint32_t kSolidMask = 0x20;

struct ProbeState {
    bool obstructed = false;
    bool unavailable = false;
    totk::core::WorldPosition hitPosition{};
    std::uint32_t timeouts = 0;
    pure::RouteProbePlan plan{};
    bool armed = false;
    std::uint32_t evaluatedThrough = 0;
    std::uint32_t requestedThrough = 0;
    std::uint32_t bypasses = 0;
};

struct ArmRequest {
    std::uint32_t rewindIndex = 0;
    bool rewinding = false;
    bool havePlayer = false;
    bool climbActive = false;
    std::uint64_t tick = 0;
    std::uint32_t generation = 0;
};

void arm(ProbeState& state, std::span<const pure::HistorySample> samples,
         const ArmRequest& request);

enum class ServiceResult : std::uint8_t { Quiet, TimedOut, Consumed };
ServiceResult service(ProbeState& state, bool rewinding, std::uint64_t tick);

void cancel(ProbeState& state);

void observe(totk::engine::RaycastFunction original, const void* from,
             const void* object);

}  // namespace self_recall::probe
