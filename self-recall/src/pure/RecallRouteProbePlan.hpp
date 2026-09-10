#pragma once

#include <span>
#include <array>
#include "RecallHistory.hpp"

namespace self_recall::pure {

constexpr std::size_t kProbeWindowMaxSamples = 12;
constexpr float kProbeWindowMaxMeters = 3.0f;
constexpr float kProbeWindowMaxDropMeters = 1.0f;
constexpr float kProbeLiftMeters = 0.45f;
constexpr float kProbeMinSegmentMeters = 0.05f;

inline std::uint32_t nextRouteProbeStart(std::uint32_t index, std::uint32_t checked,
        std::uint32_t count, bool prefetch) {
    if (!count || index >= count - 1 || checked >= count - 1) return UINT32_MAX;
    if (checked > index) {
        if (!prefetch || checked - index > kProbeWindowMaxSamples) return UINT32_MAX;
        return checked;
    }
    return index;
}

enum class RouteProbeKind : std::uint8_t { Cast, Stationary, Climb, Steep, Unavailable, Vehicle };
struct RouteProbeSegment {
    totk::core::WorldPosition from{}, to{};
    std::uint8_t fromFlags = 0, toFlags = 0;
};
struct RouteProbePlan {
    RouteProbeKind kind = RouteProbeKind::Unavailable;
    std::uint32_t through = 0; // Offset within the supplied newest-to-oldest span.
    totk::core::WorldPosition from{};
    totk::core::WorldPosition to{};
    std::array<RouteProbeSegment, kProbeWindowMaxSamples> segments{};
    unsigned count = 0;
};

inline totk::core::WorldPosition routeProbePoint(const Pose& pose) {
    const auto& r = pose.rotation.values;
    return {pose.position.x + kProbeLiftMeters * r[1],
            pose.position.y + kProbeLiftMeters * r[4],
            pose.position.z + kProbeLiftMeters * r[7]};
}

inline RouteProbePlan planRouteProbe(std::span<const HistorySample> samples, bool climbActive) {
    RouteProbePlan out{};
    if (samples.size() < 2) return out;
    const auto valid = [](const HistorySample& sample) {
        return (sample.flags & SampleAdmissible) && finitePose(sample.pose);
    };
    if (!valid(samples[0]) || !valid(samples[1])) return out;
    if (climbActive || ((samples[0].flags | samples[1].flags) & SampleClimb)) {
        out.kind = RouteProbeKind::Climb;
        out.through = 1;
        for (std::size_t i = 2; i < samples.size() && i <= kProbeWindowMaxSamples; ++i) {
            if (!valid(samples[i]) || (!climbActive &&
                !((samples[i - 1].flags | samples[i].flags) & SampleClimb))) break;
            out.through = static_cast<std::uint32_t>(i);
        }
        return out;
    }
    if ((samples[0].flags | samples[1].flags) & SampleControlStick) {
        out.kind = RouteProbeKind::Vehicle;
        out.through = 1;
        for (std::size_t i = 2; i < samples.size() && i <= kProbeWindowMaxSamples; ++i) {
            if (!valid(samples[i]) ||
                !((samples[i - 1].flags | samples[i].flags) & SampleControlStick)) break;
            out.through = static_cast<std::uint32_t>(i);
        }
        return out;
    }
    out.from = samples[0].pose.position;
    out.to = out.from;
    float distance = 0;
    for (std::size_t i = 1; i < samples.size() && i <= kProbeWindowMaxSamples; ++i) {
        if (!valid(samples[i]) || (samples[i].flags & (SampleClimb | SampleControlStick))) break;
        out.to = samples[i].pose.position;
        out.through = static_cast<std::uint32_t>(i);
        const auto from = routeProbePoint(samples[i - 1].pose);
        const auto to = routeProbePoint(samples[i].pose);
        const auto step = distance3(from, to);
        if (!std::isfinite(step)) return {};
        distance += step;
        if (step > 0)
            out.segments[out.count++] = {from, to, samples[i - 1].flags, samples[i].flags};
        if (distance >= kProbeWindowMaxMeters) break;
    }
    const auto span = distance3(out.from, out.to);
    const auto rise = std::abs(out.from.y - out.to.y);
    if (!std::isfinite(span) || !out.through) return {};
    if (rise > kProbeWindowMaxDropMeters) out.kind = RouteProbeKind::Steep;
    else if (distance < kProbeMinSegmentMeters) out.kind = RouteProbeKind::Stationary;
    else out.kind = RouteProbeKind::Cast;
    return out;
}

} // namespace self_recall::pure
