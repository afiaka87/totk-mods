// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include <cstdint>

namespace botw_abilities::wolf {

// Damage the game was about to subtract from the wolf's life and did not: from enemies and the
// world, and separately from the player, whose hits the wolf shrugs off.
struct LifeHits { int damage{}, playerDamage{}; };

[[nodiscard]] bool installLifeApi(std::uintptr_t base);
// Names the actor whose damage is intercepted and the player to ignore; zero stops watching.
void watchLife(std::uintptr_t wolf, std::uintptr_t player);
[[nodiscard]] LifeHits takeLifeHits();

}
