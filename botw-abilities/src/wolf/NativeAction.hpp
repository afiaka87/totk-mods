// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "NativeMove.hpp"

namespace botw_abilities::wolf {

bool initializeActionApi(std::uintptr_t base);

struct PathActionParameters {
    std::int32_t gearType{}, minimumGear{};
    bool allowGearDown{}, stopAndTurn{};
};

// Busy: the game is still finishing a scripted clip in the action slot (its answer 3 right after an
// Attack or BackStep, wolf-compact-08); the caller keeps its entry pending and asks again next tick.
enum class PathActionResult : std::uint8_t { Applied, Busy, Refused };

// Dispatch once per owner AI update, with enter=true only on entry to this behavior.
[[nodiscard]] PathActionResult applyPathAction(const ActorReference& actor, const MoveOutput& move,
                                               const PathActionParameters& parameters, bool enter);

// One move tick's outcome: false when the move failed or its action was refused. A busy slot keeps
// the move running with its entry still pending; the callers' own stall timers bound the wait.
[[nodiscard]] inline bool applyMoveOutput(const ActorReference& actor, const std::optional<MoveOutput>& output,
                                          const PathActionParameters& parameters, bool& enter) {
    if (!output || output->nativeResult == 3) return false;
    if (!output->applyAction) return true;
    switch (applyPathAction(actor, *output, parameters, enter)) {
    case PathActionResult::Applied: enter = false; return true;
    case PathActionResult::Busy: return true;
    default: return false;
    }
}

}
