// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include <atomic>
#include <cstdint>

namespace totk::engine {
// Abilities chosen from the ability wheel by mods. Mods built into one module share the latest
// choice, so choosing one releases L from the others, as choosing a native ability does.
enum class Ability : std::uint8_t { Stasis = 1, RemoteBomb, Cryonis };
inline std::atomic<std::uint32_t> g_abilityChoice{};

// Records a choice: a running count above the chosen ability.
inline void chooseAbility(Ability ability) {
    auto current = g_abilityChoice.load(std::memory_order_relaxed);
    while (!g_abilityChoice.compare_exchange_weak(
        current, ((current >> 8) + 1) << 8 | std::uint32_t(ability), std::memory_order_acq_rel)) {
    }
}

// True once for each new choice of a different ability.
struct AbilityChoiceWatch {
    std::uint32_t seen{};
    bool chosenElsewhere(Ability self) {
        const auto latest = g_abilityChoice.load(std::memory_order_acquire);
        if (latest == seen)
            return false;
        seen = latest;
        return (latest & 0xffu) != std::uint32_t(self);
    }
};

// An ability still answering L after another was chosen, such as a bomb left out; the latest
// choice leaves L presses to it until it lets go.
inline std::atomic<std::uint8_t> g_abilityHoldingL{};
inline void holdL(Ability ability, bool held) {
    const auto id = std::uint8_t(ability);
    auto expected = held ? std::uint8_t{} : id;
    g_abilityHoldingL.compare_exchange_strong(expected, held ? id : std::uint8_t{},
                                              std::memory_order_acq_rel);
}
inline bool lHeldElsewhere(Ability self) {
    const auto holder = g_abilityHoldingL.load(std::memory_order_acquire);
    return holder && holder != std::uint8_t(self);
}
} // namespace totk::engine
