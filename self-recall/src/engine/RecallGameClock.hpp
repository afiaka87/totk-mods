#pragma once

#include <cstdint>
#include "RecallGameTime.hpp"

namespace self_recall::game_clock {

void install(std::uintptr_t mainBase);
bool snapshot(pure::GameTimeSnapshot& out);

}  // namespace self_recall::game_clock
