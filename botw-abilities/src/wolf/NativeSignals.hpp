// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "NativeActor.hpp"

namespace botw_abilities::wolf {

bool initializeSignalApi(std::uintptr_t base);

// True while the game runs a cutscene or another scripted event.
[[nodiscard]] bool mainEventPlaying();

// Everything below runs on the wolf's AI tick, as the game's own AI nodes do.
// A howl reaching nearby enemies, at the level the stock wolf's own call uses.
void emitNoise(const ActorReference& actor, const totk::core::WorldPosition& position);
// Tells one animal to run from the position; messageId comes from Awareness::nearby.
void sendFlee(const ActorReference& actor, std::uint32_t messageId, const totk::core::WorldPosition& position);

// The game's own head-turn controller, aimed at a point in the world.
class HeadLook {
public:
    void aim(const ActorReference& actor, const totk::core::WorldPosition& point);
    void rest(const ActorReference& actor);
private:
    struct Request {
        std::uint32_t setting{}, variation{}, activation = 1;
        totk::core::WorldPosition position{};
        std::uint32_t reservedA{};
        float yOffset{};
        std::uint32_t reservedB{}, priority = 2, kind = 18, tail = 4;
    };
    static_assert(sizeof(Request) == 48);
    Request request_;
    bool active_{}, refused_{};
};

}
