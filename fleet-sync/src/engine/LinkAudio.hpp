#pragma once

#include <cstdint>

namespace linked_stick::engine::audio {
void initialize(std::uintptr_t mainBase);
bool play(const char* cue);
}
