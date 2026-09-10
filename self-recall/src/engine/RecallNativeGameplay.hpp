#pragma once
#include <cstdint>
#include "RecallStamina.hpp"

namespace self_recall::native_gameplay {
void install();
pure::StaminaStatus stamina(const void* player);
bool begin(const void* player);
void release();
void reset();
} // namespace self_recall::native_gameplay
