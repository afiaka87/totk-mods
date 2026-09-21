// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include "CaptureMath.hpp"
#include "TargetValidator.hpp"
#include "YawEasing.hpp"

namespace arrowbound::pure {
struct WallGripConfig {
    // Local confirmation and safety limits, not native physics constants.
    float probeHalfSpan = 0.5f;
    float contactTolerance = 0.5f;
    float maxApproachDistance = 4.0f;
    float minOutsideDistance = 0.05f;
    int probeTimeoutTicks = 24;
};

inline bool wallProbeSegment(Vec3 contact, Vec3 direction, Vec3& from, Vec3& to,
                             const WallGripConfig& config = {}) {
    const float speed = length(direction);
    if (!finite3(contact) || !finite3(direction) || !std::isfinite(speed) || speed < 0.5f)
        return false;
    const Vec3 half = mul(direction, config.probeHalfSpan / speed);
    from = sub(contact, half);
    to = add(contact, half);
    return finite3(from) && finite3(to);
}

inline Verdict wallGripSurface(TargetSample sample, Vec3 impact, Vec3 link,
                                std::uint32_t generation, std::uint32_t request,
                                const WallGripConfig& config = {}) {
    if (!request || sample.sequence != request || sample.generation != generation)
        return Verdict::Pending;
    if (!finite3(impact) || !finite3(link)) return Verdict::InvalidFinite;
    sample.range = distance(link, sample.position);
    ValidatorConfig validator{};
    validator.minRange = 0;
    validator.maxRange = config.maxApproachDistance;
    const auto verdict = validate(sample, {generation, request, 0}, validator);
    if (verdict != Verdict::Valid) return verdict;
    if (distance(sample.position, impact) > config.contactTolerance)
        return Verdict::BlockedCorridor;
    const Vec3 normal = mul(sample.normal, 1.0f / length(sample.normal));
    if (dot(sub(link, sample.position), normal) < config.minOutsideDistance)
        return Verdict::Backface;
    return Verdict::Valid;
}

inline bool wallGripBasis(const float basis[9], Vec3 normal, Vec3 direction, float out[9]) {
    if (!finiteBasis(basis)) return false;
    float yaw = 0;
    return signedHorizontalYaw({basis[2], 0, basis[8]},
        wallFacingDirection(normal, direction), yaw) && rotateBasisWorldYaw(basis, yaw, out);
}

inline bool wallGripOwnsInput(bool active, bool npadValid, std::uint32_t target,
                              std::uint32_t controller, std::uint64_t samplingNumber) {
    return active && npadValid && target == controller && samplingNumber != 0;
}

enum class GripProgress { Waiting, Acquired, Rejected, Stalled, LeftGlider, TimedOut };

struct WallGripWatch {
    std::uint64_t startedTick = 0;
    std::uint32_t climbBaseline = 0;
    std::uint32_t lastUpdates = 0;
    int stalled = 0;
    int leaveGrace = 0;

    GripProgress update(std::uint64_t tick, std::uint32_t climb, bool rejected,
                        bool parasail, std::uint32_t updates, const CaptureConfig& config = {}) {
        if (climb != climbBaseline) return GripProgress::Acquired;
        if (rejected) return GripProgress::Rejected;
        if (tick < startedTick || tick - startedTick >= static_cast<std::uint64_t>(config.timeoutTicks))
            return GripProgress::TimedOut;
        if (parasail) {
            leaveGrace = 0;
            if (updates == lastUpdates) {
                if (++stalled > 10) return GripProgress::Stalled;
            } else {
                stalled = 0;
                lastUpdates = updates;
            }
        } else if (++leaveGrace > config.leaveGraceTicks) {
            return GripProgress::LeftGlider;
        }
        return GripProgress::Waiting;
    }
};
} // namespace arrowbound::pure
