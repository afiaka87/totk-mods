#pragma once

#include "RecallWristProvider.hpp"

namespace self_recall::wrist_effects {

void install(std::uintptr_t mainBase);
void begin(std::uint32_t historyGeneration, std::span<const pure::CompactEffectHandle> handles);
void end();

} // namespace self_recall::wrist_effects
