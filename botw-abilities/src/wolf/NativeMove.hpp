// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#pragma once
#include "NativeActor.hpp"
#include <optional>

namespace botw_abilities::wolf {

bool initializeMoveApi(std::uintptr_t base);

struct MoveParameters {
    std::int32_t actionVerb{}, minGear{}, maxGear{}, gearType{}, minimumSlowGear{};
    float goalTolerance{};
    bool waitAsync{}, riderGear{}, autoSlow{}, decelerate{}, waitUntilPath{}, keepGearLimit{};
};

struct MoveOutput {
    std::uint32_t nativeResult{};
    std::int32_t actionVerb{}, gear{};
    bool applyAction = true;
};

// Call from the actor's AI update with a fresh scoped reference on every invocation.
class NativeMove {
public:
    NativeMove() = default;
    NativeMove(const NativeMove&) = delete;
    NativeMove& operator=(const NativeMove&) = delete;
    [[nodiscard]] bool start(const ActorReference& actor, const MoveParameters& parameters,
                             const totk::core::WorldPosition& destination);
    [[nodiscard]] bool retarget(const ActorReference& actor, const totk::core::WorldPosition& destination);
    [[nodiscard]] std::optional<MoveOutput> update(const ActorReference& actor, const MoveParameters& parameters,
                                                   std::optional<int> desiredGear = std::nullopt);
    void stop(const ActorReference& actor);
    [[nodiscard]] bool owns(const ActorReference& actor) const;
private:
    std::uintptr_t owner_{};
    std::uint32_t generation_{};
    struct State {
        std::uint32_t result{};
        std::int32_t selectedGear{}, gearLimit{};
        std::uint8_t flags{}, padding[3]{};
    } state_{};
    static_assert(sizeof(State) == 16 && offsetof(State, flags) == 12);
    bool keepGearLimit_{};
};
static_assert(sizeof(NativeMove) == 32);

}
