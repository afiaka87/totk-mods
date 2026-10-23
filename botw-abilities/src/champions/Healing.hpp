// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include "totk/engine/Pointer.hpp"

namespace botw_abilities::champions {

struct LifeMessagePayload {
    std::int32_t health = 0;
    std::int32_t maxHealth = 0;
    std::int32_t extraHealth = 20;
    std::int32_t brokenHealth = 0;
    std::uint16_t flags = 0x800;
};

struct LifeMessage {
    std::uint32_t reserved = 0;
    std::uint32_t id = 0x46030000;
    std::uintptr_t sender = 0;
    const LifeMessagePayload* payload = nullptr;
};

static_assert(offsetof(LifeMessage, id) == 4);
static_assert(offsetof(LifeMessage, payload) == 16);
static_assert(offsetof(LifeMessagePayload, extraHealth) == 8);
static_assert(offsetof(LifeMessagePayload, flags) == 16);

struct HealingFunctions {
    bool (*receive)(std::uintptr_t life, const LifeMessage* message) = nullptr;
    std::uintptr_t (*process)(std::uintptr_t life, std::uintptr_t context,
                             std::uint8_t includePendingHeal) = nullptr;
};

enum class HealingError : std::uint8_t {
    None, MissingFunction, MissingLife, MissingValues, InvalidMaxHealth, MessageRefused,
};

struct HealingResult {
    std::int32_t health = 0;
    HealingError error = HealingError::None;
    explicit operator bool() const { return error == HealingError::None; }
};

inline std::uintptr_t playerLife(std::uintptr_t healExtension) {
    using namespace totk::engine;
    if (!isPlausibleAddress(healExtension)) return 0;
    const auto actor = readMemory<std::uintptr_t>(healExtension + 8);
    if (!isPlausibleAddress(actor)) return 0;
    const auto registry = readMemory<std::uintptr_t>(actor + 552);
    if (!isPlausibleAddress(registry)) return 0;
    const auto life = readMemory<std::uintptr_t>(registry + 760);
    return isPlausibleAddress(life) ? life : 0;
}

// Invoke only from calcHealLifeDepleted, after vanilla one-shot protection has declined.
inline HealingResult prepareMiphaHealing(std::uintptr_t healExtension,
                                         std::uintptr_t context, HealingFunctions functions) {
    using namespace totk::engine;
    if (!functions.receive || !functions.process) return {0, HealingError::MissingFunction};
    const auto life = playerLife(healExtension);
    if (!life) return {0, HealingError::MissingLife};
    const auto health = readMemory<std::uintptr_t>(life + 6192);
    const auto maximum = readMemory<std::uintptr_t>(life + 6200);
    const auto extra = readMemory<std::uintptr_t>(life + 6208);
    if (!isPlausibleAddress(health) || !isPlausibleAddress(maximum) || !isPlausibleAddress(extra))
        return {0, HealingError::MissingValues};
    const auto maxHealth = readMemory<std::int32_t>(maximum + 8);
    if (maxHealth <= 0) return {0, HealingError::InvalidMaxHealth};

    const LifeMessagePayload payload{};
    const LifeMessage message{0, 0x46030000, 0, &payload};
    if (!functions.receive(life, &message)) return {0, HealingError::MessageRefused};
    functions.process(life, context, 0);
    return {maxHealth};
}

} // namespace botw_abilities::champions
