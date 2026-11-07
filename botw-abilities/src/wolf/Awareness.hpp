// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "NativeActor.hpp"
#include "FoodTable.hpp"
#include "totk/engine/Scene.hpp"
#include <algorithm>
#include <optional>
#include <string_view>

namespace botw_abilities::wolf {

[[nodiscard]] constexpr std::uint32_t foodHash(std::string_view name) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const char c : name) {
        crc ^= std::uint8_t(c);
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

// Meals carry a value set when cooked; twelve is three hearts, the wolf's whole default life.
[[nodiscard]] constexpr int foodRecover(std::string_view name) {
    if (name.starts_with("Item_Cook_")) return 12;
    const auto key = foodHash(name);
    const auto* entry = std::lower_bound(kFoods.begin(), kFoods.end(), key,
        [](const FoodEntry& food, std::uint32_t value) { return food.name < value; });
    return entry != kFoods.end() && entry->name == key ? entry->recover : 0;
}

[[nodiscard]] constexpr bool isPrey(std::string_view name) {
    return name.starts_with("Animal_") && !name.starts_with("Animal_Insect") &&
           !name.starts_with("Animal_Fish") && !name.starts_with("Animal_Bird");
}

struct AwareActor {
    std::uintptr_t address{};
    std::uint32_t id{};
    totk::core::WorldPosition position{};
    bool present{}, flag{};  // flag: prey for a target, dropped by the player for food.
    int recover{};
};

struct Awareness {
    AwareActor target, food;
    bool deepWater{};
    std::uint32_t nearby[8]{};  // Message ids of animals close enough to startle.
    unsigned nearbyCount{};
};

bool initializeAwarenessApi(std::uintptr_t base);
// Serialized on the controller thread, which also owns the roster walk.
void scanSurroundings(const totk::engine::SceneContext& scene, std::uint32_t wolfId,
                      const totk::core::WorldPosition& wolf, const totk::core::WorldPosition& player);
[[nodiscard]] Awareness surroundings();
void restrictTarget(std::uint32_t id, float ticks);
void avoidTarget(std::uint32_t id);
void restrictFood(std::uint32_t id);
void consumeFood(std::uint32_t id);
void clearSurroundings();
[[nodiscard]] std::optional<int> readLife(const ActorReference& actor);
void writeLife(const ActorReference& actor, int life);
[[nodiscard]] bool moveActor(const ActorReference& actor, const totk::core::WorldTransform& transform);

}
