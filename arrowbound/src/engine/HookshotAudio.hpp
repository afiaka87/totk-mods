// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Plays the game's own sounds through sound users the game keeps registered. Which sound belongs to which moment is
// decided in pure/HookshotCues.hpp.
#pragma once

#include "EngineNamespace.hpp"

#include <cstdint>
#include "Vec3.hpp"

namespace HOOKSHOT_ENGINE_NS::audio {

void initialize(std::uintptr_t mainBase);

// Find the interface speaker early so the first cue does not pay for the walk.
void prime();
bool ready();

// One-shot through the interface speaker (UI_GlobalSound). True only when the
// game handed back a live handle.
bool playCue(const char* cueName);

// One-shot through a named sound user. False when that user is not registered,
// has no live instance, or refused the cue.
bool playCue(const char* userName, const char* cueName);

// Shared travel/arrival audio verifies asset start and retains no scene-owned pointers.
void playAbilityCue(bool arrival, pure::Vec3 position);
void startArrowFollowLoop(pure::Vec3 position);
void stopArrowFollowLoop();
void updateAbilityCues(pure::Vec3 position);
void resetAbilityCues();

// Whether a named user is registered right now and how many instances it has.
bool describeUser(const char* userName, int& instances);

}  // namespace HOOKSHOT_ENGINE_NS::audio
