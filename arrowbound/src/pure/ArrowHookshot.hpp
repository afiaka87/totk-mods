// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>

#include "Vec3.hpp"

namespace arrowbound::pure {

enum class ArrowPhase : std::uint8_t {
    Idle,
    WaitingForArrow,
    Following,
    WallProbe,
    WallGrip,
    Bailout,
};

struct ArrowInputDecision {
    bool cancel = false;
    bool reaim = false;
    bool consumeB = false;
};

class ArrowInputOwnership {
    bool ownsB_ = false;

public:
    ArrowInputDecision update(ArrowPhase phase, bool bHeld, bool bEdge, bool zrEdge) {
        if (!bHeld) ownsB_ = false;
        const bool engaged = phase != ArrowPhase::Idle;
        const bool cancel = engaged && bEdge;
        if (cancel) ownsB_ = true;
        return {cancel, engaged && zrEdge && !cancel, ownsB_};
    }
};

inline bool arrowBailoutReady(bool parasailActive, std::uint32_t updates,
                              std::uint32_t updatesAtBailout) {
    return parasailActive && updates != updatesAtBailout;
}

inline bool arrowClaimIsNew(std::uint32_t nativeState) {
    // Native state 0 is observed before the first update; 1 already belongs to an older update.
    return nativeState == 0;
}

inline bool arrowHasFlight(ArrowPhase phase, std::uint32_t shot, std::uint32_t seen) {
    return phase != ArrowPhase::Idle || shot != seen;
}

inline const char* arrowPhaseName(ArrowPhase phase) {
    switch (phase) {
        case ArrowPhase::Idle:            return "IDLE";
        case ArrowPhase::WaitingForArrow: return "WAITING";
        case ArrowPhase::Following:       return "FOLLOWING";
        case ArrowPhase::WallProbe:       return "WALL_PROBE";
        case ArrowPhase::WallGrip:        return "WALL_GRIP";
        case ArrowPhase::Bailout:         return "BAILOUT";
    }
    return "?";
}

struct ArrowConfig {
    float trailDistance = 1.25f;
    float followUpdatesPerSecond = 60.0f;
    int claimTimeoutTicks = 30;
    int updateTimeoutTicks = 18;
    int bailoutTimeoutTicks = 90;
};

inline bool arrowMotionReady(Vec3 arrowPosition, Vec3 velocity) {
    const float speed = length(velocity);
    return finite3(arrowPosition) && finite3(velocity) &&
           std::isfinite(speed) && speed >= 0.01f;
}

inline bool arrowFollowVelocity(Vec3 bodyVelocity, float actorRate, Vec3& out) {
    if (!finite3(bodyVelocity) || !std::isfinite(actorRate) || actorRate < 0) return false;
    // Native arrow motion includes delta/(delta/rate); zero uses the native fallback of 1.
    const float rate = actorRate == 0 ? 1.0f : actorRate;
    out = mul(bodyVelocity, 1.0f / rate);
    return finite3(out);
}

inline bool arrowTrailPoint(Vec3 arrowPosition, Vec3 velocity, Vec3& out,
                            const ArrowConfig& config = {}) {
    if (!arrowMotionReady(arrowPosition, velocity) ||
        !std::isfinite(config.trailDistance) || config.trailDistance < 0.0f)
        return false;
    const float speed = length(velocity);
    out = sub(arrowPosition, mul(velocity, config.trailDistance / speed));
    return finite3(out);
}

class ArrowFollower {
    Vec3 position_{};
    Vec3 reference_{};
    Vec3 samplePosition_{};
    std::uint64_t tick_ = 0;
    bool started_ = false;
    float correctionDistance_ = 0;

public:
    // Presentation tuning, not native physics: settle drift over 0.25 s, at most 25% per step.
    static constexpr float kCorrectionSeconds = 0.25f;
    static constexpr float kCorrectionStepFraction = 0.25f;

    float correctionDistance() const { return correctionDistance_; }

    bool update(std::uint64_t tick, Vec3 position, Vec3 velocity, bool fresh,
                Vec3& outPosition, Vec3& outVelocity, const ArrowConfig& config = {}) {
        if (!arrowMotionReady(position, velocity) ||
            !std::isfinite(config.followUpdatesPerSecond) || config.followUpdatesPerSecond <= 0)
            return false;
        if (!started_) {
            if (!fresh) return false;
            position_ = reference_ = samplePosition_ = position;
            tick_ = tick;
            started_ = true;
        } else if (tick != tick_) {
            // The caller advances once per gameplay update, never catch up a missing burst.
            if (tick < tick_ || tick - tick_ != 1) return false;
            const float dt = 1.0f / config.followUpdatesPerSecond;
            const Vec3 step = mul(velocity, dt);
            if (fresh && distance(position, samplePosition_) > 0.0001f) {
                reference_ = samplePosition_ = position;
            } else {
                reference_ = add(reference_, step);
            }
            const Vec3 next = add(position_, step);
            Vec3 correction = mul(sub(reference_, next), clamp01(dt / kCorrectionSeconds));
            const float correctionLength = length(correction);
            const float limit = length(step) * kCorrectionStepFraction;
            if (!std::isfinite(correctionLength) || !std::isfinite(limit)) return false;
            if (correctionLength > limit && correctionLength > 0)
                correction = mul(correction, limit / correctionLength);
            position_ = add(next, correction);
            correctionDistance_ = length(correction);
            tick_ = tick;
        }
        outPosition = position_;
        outVelocity = velocity;
        return finite3(outPosition);
    }
};

inline std::uint64_t arrowFlightBowResult(std::uint64_t native, bool following) {
    // ExecutePlayerBow::getResult: 0 running, 2 completed, 3 unavailable/failed.
    return following && native == 0 ? 2 : native;
}

inline std::uint64_t arrowFlightParasailEntry(std::uint64_t native, bool following) {
    // The glider entry tests bit 0 to select its existing steady-glide command and phase.
    return following ? native | 1ULL : native;
}

template <class Trip>
inline void retireArrowTrip(Trip& trip, std::uint32_t shotSeq) {
    trip = {};
    trip.shotSeqSeen = shotSeq;
}

enum class ArrowImpactSource : std::uint32_t { Sensor, WorldSweep };

struct ArrowImpact {
    ArrowImpactSource source = ArrowImpactSource::Sensor;
    Vec3 contact{};
    Vec3 direction{};
    int sensorType = 0;
    bool water = false;

    bool wallCandidate() const {
        return !water && (source == ArrowImpactSource::WorldSweep || sensorType == 6);
    }
};

enum class ImpactPublication { NoHit, Invalid, Duplicate, Published };

template <class Publish>
ImpactPublication publishArrowImpact(bool nativeHit, const ArrowImpact& impact,
                                     std::atomic<std::uint32_t>& published, Publish publish) {
    if (!nativeHit) return ImpactPublication::NoHit;
    if (!finite3(impact.contact) || !finite3(impact.direction) || length(impact.direction) < 0.5f)
        return ImpactPublication::Invalid;
    if (published.exchange(1, std::memory_order_acq_rel)) return ImpactPublication::Duplicate;
    publish(impact);
    return ImpactPublication::Published;
}

inline bool arrowTimedOut(std::uint64_t tick, std::uint64_t lastTick,
                          int limit) {
    return limit > 0 && tick > lastTick &&
           tick - lastTick > static_cast<std::uint64_t>(limit);
}

}  // namespace arrowbound::pure
