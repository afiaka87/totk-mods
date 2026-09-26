// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Ability sounds shared by Arrowbound and Glideshot.
#pragma once

namespace arrowbound::pure {
// Travel cues prefer Ultrahand's sounds, with interface fallbacks; AmiiboMarker_OK loops forever, so never use it.
inline constexpr const char* kCueAbilityUser = "ExpressionSound";
inline constexpr const char* kCueTravel = "UltraHand_Start";
inline constexpr const char* kCueTravelFallback = "mc_PlusMenuOpen";
inline constexpr const char* kCueArrive = "UltraHand_End";
inline constexpr const char* kCueArriveFallback = "MapMarker_1";
inline constexpr const char* kCueArrowFollowLoop = "ReverseRecorder_Lp";

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
}
