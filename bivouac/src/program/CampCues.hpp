// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

#pragma once

#include <cstdint>

namespace bivouac::cues {

enum class Cue : std::uint8_t {
    None = 0,
    Placed,
    Refused,
    Travel,
    Removed,
};

// Interface-speaker cues (UI_GlobalSound bank). AmiiboMarker_OK is banned: it loops.
inline constexpr const char* kPlaced = "mc_HeartUp_Short";
inline constexpr const char* kRefused = "mc_AmiiboError";
inline constexpr const char* kTravel = "MapMarker_1";
inline constexpr const char* kRemoved = "AmiiboMarker_Sign";

constexpr const char* cueName(Cue cue) {
    switch (cue) {
    case Cue::Placed:  return kPlaced;
    case Cue::Refused: return kRefused;
    case Cue::Travel:  return kTravel;
    case Cue::Removed: return kRemoved;
    case Cue::None:    return nullptr;
    }
    return nullptr;
}

static_assert(cueName(Cue::None) == nullptr);
static_assert(cueName(Cue::Placed) != nullptr);

}  // namespace bivouac::cues
