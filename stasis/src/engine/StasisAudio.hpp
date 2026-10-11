// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
namespace stasis::audio {
enum class Cue : unsigned { Start, Hit, HitEnd, End, EndMedium, EndStrong, Countdown };
void install();
void play(Cue cue);
} // namespace stasis::audio
