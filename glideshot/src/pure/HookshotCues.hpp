// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Which game sound belongs to which moment, and how often it may repeat.
#pragma once

#include <cstdint>

#include "HookshotState.hpp"
#include "TargetValidator.hpp"

namespace zonai_hookshot::pure {

// Interface-speaker cues (UI_GlobalSound bank), all heard on this path.
inline constexpr const char* kCueAim = "MapMarker_1";          // reticle appears
inline constexpr const char* kCueAccept = "mc_HeartUp_Short";  // target went green
inline constexpr const char* kCueReject = "mc_AmiiboError";    // target went red
inline constexpr const char* kCueFire = "AmiiboMarker_Sign";   // the shot commits

// Travel start and arrival prefer Ultrahand's own activation and cancel sounds, which live in the ExpressionSound
// user. That user is not always registered, so each has a heard interface-speaker fallback.

// AmiiboMarker_OK is banned: it loops until the game closes.
inline constexpr const char* kCueAbilityUser = "ExpressionSound";
inline constexpr const char* kCueTravel = "UltraHand_Start";
inline constexpr const char* kCueTravelFallback = "mc_PlusMenuOpen";
inline constexpr const char* kCueArrive = "UltraHand_End";
inline constexpr const char* kCueArriveFallback = "MapMarker_1";

// Fall back once when an allocated event never starts a playable asset.
struct AbilityCueGate {
    unsigned checks = 0;
    bool resolved = false;

    bool needsFallback(bool eventValid, unsigned liveAssets) {
        if (resolved) return false;
        if (eventValid && liveAssets) {
            resolved = true;
            return false;
        }
        if (!eventValid || ++checks >= 4) {
            resolved = true;
            return true;
        }
        return false;
    }
};

// The reject beep can be machine-gunned by sweeping a bad wall: one per 250 ms
// at the ~57 Hz input driver.
inline constexpr std::uint32_t kCueMinRepeatTicks = 14;

inline const char* cueForEvent(Event event) {
    switch (event) {
        case Event::TargetingEntered: return kCueAim;
        case Event::Committed:        return kCueFire;
        case Event::ClimbAcquired:    return kCueArrive;
        default:                      return nullptr;
    }
}

// The green/red beeps are driven by the verdict changing; sweeping bare sky
// (Pending/Miss) stays silent.
inline const char* cueForVerdictChange(Verdict previous, Verdict current) {
    if (previous == current) return nullptr;
    if (current == Verdict::Valid) return kCueAccept;
    if (current == Verdict::Pending || current == Verdict::Miss ||
        current == Verdict::InvalidFinite)
        return nullptr;
    return kCueReject;
}

struct CueRateLimit {
    std::uint32_t lastTick = 0;
    bool started = false;

    bool allow(std::uint32_t tick, std::uint32_t minGap = kCueMinRepeatTicks) {
        if (started && tick - lastTick < minGap) return false;
        lastTick = tick;
        started = true;
        return true;
    }
};

struct AimFeedbackInput {
    bool active = false;
    Verdict verdict = Verdict::Pending;
};

inline AimFeedbackInput aimFeedbackInput(Phase phase, Verdict manual) {
    if (phase == Phase::Targeting || phase == Phase::Confirming) return {true, manual};
    return {};
}

struct AimFeedbackCues {
    const char* entry = nullptr;
    const char* verdict = nullptr;
};

struct AimFeedbackState {
    bool active = false;
    Verdict lastVerdict = Verdict::Pending;
    CueRateLimit rejectGate{};

    AimFeedbackCues update(AimFeedbackInput input, std::uint32_t tick) {
        AimFeedbackCues cues{};
        if (input.active && !active) cues.entry = kCueAim;
        const auto now = input.active ? input.verdict : Verdict::Pending;
        const char* change = cueForVerdictChange(lastVerdict, now);
        if (change && (change != kCueReject || rejectGate.allow(tick))) cues.verdict = change;
        active = input.active;
        lastVerdict = now;
        return cues;
    }
};

inline bool travelCueWanted(Phase phase) {
    return phase == Phase::PositionCruise || phase == Phase::Capture ||
           phase == Phase::FallCruise || phase == Phase::GlideHandoff ||
           phase == Phase::GlideTerminal;
}

// One emission per continuous travel interval; a refused emit does not retry.
struct TravelCueGate {
    bool armed = true;

    bool shouldEmit(bool wanted) {
        if (!wanted) {
            armed = true;
            return false;
        }
        if (!armed) return false;
        armed = false;
        return true;
    }

    void reset() { armed = true; }
};

}  // namespace zonai_hookshot::pure
