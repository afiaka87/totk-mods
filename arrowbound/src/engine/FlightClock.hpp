// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "../pure/FlightClock.hpp"
namespace arrowbound::game_clock {
void install(std::uintptr_t mainBase);
bool snapshot(pure::FlightTime& out);
}
