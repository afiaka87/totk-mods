// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Cruise velocity law and bounded trip guards for the laboratory Fall lane.
#pragma once

#include <cstdint>

#include "Vec3.hpp"

namespace zonai_hookshot::pure {
struct TransportConfig {
    // Speed tiers in metres per second.
    float speeds[3] = {20.0f, 40.0f, 60.0f};
    float arriveRadius = 3.0f;      // stop driving inside this distance
    float brakeSeconds = 0.75f;     // braking window: distance = speed * this
    float minBrakeSpeed = 4.0f;     // never brake below this until arrival
    int tripTimeoutTicks = 900;     // absolute trip bound (~15-30 s of ticks)
    int progressWindowTicks = 60;   // must gain ground within this window
    float progressMinDelta = 0.25f; // "gained ground" = remaining shrank this much
};

inline Vec3 cruiseVelocity(const Vec3& linkPos, const Vec3& target, float speed,
                           const TransportConfig& c = {}) {
    const Vec3 toTarget = sub(target, linkPos);
    const float remaining = length(toTarget);
    if (!finite3(toTarget) || remaining < 1e-3f) return {};
    float chosen = speed;
    const float brakeDistance = speed * c.brakeSeconds;
    if (brakeDistance > 1e-3f && remaining < brakeDistance) {
        chosen = speed * (remaining / brakeDistance);
        if (chosen < c.minBrakeSpeed) chosen = c.minBrakeSpeed;
    }
    return mul(toTarget, chosen / remaining);
}

// Handoff distance: arrival radius plus half a second of travel, floored so slow tiers still
// glide.
inline float handoffDistance(float speed, const TransportConfig& c = {}) {
    const float d = c.arriveRadius + 0.5f * speed;
    return d < 12.0f ? 12.0f : d;
}

enum class TripCheck : uint8_t {
    Continue,
    Arrived,     // remaining inside the arrival radius
    Timeout,     // absolute trip bound exhausted
    NoProgress,  // remaining has not shrunk within the progress window
    BadValues,   // non-finite remaining - stop driving immediately
};

inline const char* tripCheckName(TripCheck t) {
    switch (t) {
        case TripCheck::Continue:   return "CONTINUE";
        case TripCheck::Arrived:    return "ARRIVED";
        case TripCheck::Timeout:    return "TRIP TIMEOUT";
        case TripCheck::NoProgress: return "NO PROGRESS";
        case TripCheck::BadValues:  return "BAD VALUES";
    }
    return "?";
}

struct TripState {
    float bestRemaining = 0.0f;  // init to the committed span at trip start
    int ticksSinceProgress = 0;
    int ticksTotal = 0;
};

inline TripCheck checkTrip(TripState& s, float remaining,
                           const TransportConfig& c = {}) {
    if (!(remaining == remaining) || remaining > 1.0e6f || remaining < 0.0f)
        return TripCheck::BadValues;
    if (remaining <= c.arriveRadius) return TripCheck::Arrived;
    if (++s.ticksTotal >= c.tripTimeoutTicks) return TripCheck::Timeout;
    if (remaining < s.bestRemaining - c.progressMinDelta) {
        s.bestRemaining = remaining;
        s.ticksSinceProgress = 0;
    } else if (++s.ticksSinceProgress >= c.progressWindowTicks) {
        return TripCheck::NoProgress;
    }
    return TripCheck::Continue;
}

}  // namespace zonai_hookshot::pure
