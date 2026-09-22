// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "../pure/FlightClock.hpp"
namespace arrowbound::game_clock {
void install(std::uintptr_t mainBase);
bool snapshot(pure::FlightTime& out);
}
