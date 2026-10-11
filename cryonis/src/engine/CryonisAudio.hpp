// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <cstdint>
namespace cryonis::audio {
enum class Cue : unsigned { Start, Making, End, Crush, Preview };
void install(std::uintptr_t);
void play(Cue);
void preview(bool active);
}
