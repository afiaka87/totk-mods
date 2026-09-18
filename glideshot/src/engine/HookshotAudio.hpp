// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis

// Plays the game's own sounds through sound users the game keeps registered. Which sound belongs to which moment is
// decided in pure/HookshotCues.hpp.
#pragma once

#include <cstdint>

namespace zonai_hookshot::audio {

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

// Whether a named user is registered right now and how many instances it has.
bool describeUser(const char* userName, int& instances);

}  // namespace zonai_hookshot::audio
