// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "ChampionSounds.hpp"
#include "SoundEvents.hpp"
#include <cstdint>

namespace botw_abilities::champions::audio {
bool install(std::uintptr_t base);
bool play(Cue cue);
bool sustain(Cue cue, bool active);
bool stop(Cue cue);
bool stopAll();
// The gameplay service owns event ordering and retries refused transitions on its next tick.
bool update(const SoundActivity& active);
} // namespace botw_abilities::champions::audio
