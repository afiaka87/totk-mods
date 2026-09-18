// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Launch bookkeeping for the laboratory lane: a synthetic X press (rising edge + held sample) plus
// a one-shot 4x jump boost; airborne origins inject nothing.
#pragma once

namespace zonai_hookshot::pure {
enum class LaunchOrigin : unsigned char {
    Solid,     // ground or climbing: inject X, arm the one-shot boost
    Airborne,  // native Fall already owns Link: wait for its next update
    Parasail,  // native glide owns Link: unsupported start, refuse cleanly
};

inline LaunchOrigin classifyOrigin(bool fallActive, bool parasailActive) {
    if (parasailActive) return LaunchOrigin::Parasail;
    if (fallActive) return LaunchOrigin::Airborne;
    return LaunchOrigin::Solid;
}

struct LaunchPulse {
    int pulseFramesLeft = 0;
    bool boostArmed = false;
};

constexpr int kPulseFrames = 2;  // one rising edge + one held sample

inline bool beginLaunch(LaunchPulse& s, LaunchOrigin origin) {
    if (origin != LaunchOrigin::Solid) return false;
    s.pulseFramesLeft = kPulseFrames;
    s.boostArmed = true;
    return true;
}

// The counter decrements only when a live sample is actually mutated; a held physical X never gets
// a manufactured edge.
inline bool pulseMutates(LaunchPulse& s, bool freshSample, bool physicalXHeld) {
    if (s.pulseFramesLeft <= 0 || !freshSample || physicalXHeld) return false;
    --s.pulseFramesLeft;
    return true;
}

// Every non-success exit clears the boost so the player's next unrelated jump cannot inherit it.
inline void clearLaunch(LaunchPulse& s) {
    s.pulseFramesLeft = 0;
    s.boostArmed = false;
}

inline bool takeBoost(LaunchPulse& s) {
    const bool had = s.boostArmed;
    s.boostArmed = false;
    return had;
}

inline float boostedJumpValue(float vanilla, bool boostConsumed) {
    return boostConsumed ? vanilla * 4.0f : vanilla;
}

}  // namespace zonai_hookshot::pure
