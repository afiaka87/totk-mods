// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include "pure/DescendGeometry.hpp"

#include <cstdint>

#define DESCEND_INTEGER_ONLY __attribute__((target("general-regs-only")))

namespace descend::feature {

void onNpad(void* device);

// Pending x before the clipper update runs; a surviving sentinel means the update skipped its query.
struct ClipperUpdate {
    bool armed = false;
    std::uint32_t savedX = 0;
};
ClipperUpdate beforeClipperUpdate(void* clipper);
void afterClipperUpdate(void* clipper, void* context, const ClipperUpdate& before);

// Runs inside the marker inline hook: integer registers only.
DESCEND_INTEGER_ONLY void onMarkerCast(std::uintptr_t query, std::uintptr_t clipper);
DESCEND_INTEGER_ONLY bool warmMarker();
bool blendMarker(std::uintptr_t clipper, unsigned greenOffset, unsigned effect);
void beginMarkerPostCalc(void* clipper, void* context);
void endMarkerPostCalc();
void scaleMarker(void* handle, const pure::Vec3* position);

void beforeSelectorPosition(void* selector);
bool ownsRiseStage(void* node);
bool holdsTravelTarget(void* actor, const void* in);
pure::Vec3 preloadPosition(const pure::Vec3& position, std::uintptr_t actor = 0);
void afterCameraTarget(void* component);

// In the preview, a running timer without a cancel request means the stage left on the confirm press.
struct TravelState {
    bool owned = false;
    std::uint32_t state = 0;
    float timer = 0;
    bool cancelled = false;
    bool hold = false;
};
TravelState beforeTravelState(void* node);
void afterTravelState(void* node, const TravelState& before);
bool overrideTravelAnimation(void* node);

// Runs inside the cancel stage's enter, before it plays its first animation: integer registers only. Returns the command to play: the fall instead of the swirl right after A.
DESCEND_INTEGER_ONLY std::uintptr_t cancelCommand(std::uintptr_t player, std::uintptr_t command);
bool beforeCancelStage(void* node);
void afterCancelStage(void* node);
void afterCancelLeave(void* node);

// Runs inside the A-prompt inline hooks, right after the prompt's message was found: integer registers only. While Descend aim is on, turns the "Ascend" message into "Descend" (setText) or only marks it with the same id (the screen's per-frame check, so it redraws once per change).
DESCEND_INTEGER_ONLY void onPromptMessage(std::uintptr_t message, std::uintptr_t label, bool setText);

// Keep native screen-cover state consistent through effect calculation, scene preparation and subsequent draws. A stale/retired event clears only cover we forced on the same still-current graphics owner.
DESCEND_INTEGER_ONLY void synchronizeScreenCover();
DESCEND_INTEGER_ONLY std::uint64_t effectDirection(std::uint64_t native);

// The game's camera-distance fade of Link, measured from the Descend aim view while it is shown: the camera floats it reads, saved for the duration of the fade update.
struct CameraFade {
    bool saved = false;
    pure::Vec3 position{}, lookAt{};
};
CameraFade beforeCameraAlpha(void* component);
void afterCameraAlpha(void* component, const CameraFade& saved);
// The same fade in the player camera's own step, the one that runs while Ascend aims (no event camera).
CameraFade beforePlayerFade(void* camera);
void afterPlayerFade(void* camera, const CameraFade& saved);

// The game's own up direction, saved while the camera update uses a level one.
struct CameraUp {
    bool saved = false;
    pure::Vec3 up{};
    std::uint32_t uncoverSerial = 0;
    std::uint64_t previewTick = 0;
};
CameraUp beforeCameraUpdate(void* component);
void afterCameraUpdate(void* component, const CameraUp& saved);

} // namespace descend::feature
