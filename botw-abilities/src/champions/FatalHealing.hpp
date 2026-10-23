// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "Healing.hpp"
#include "SavedState.hpp"

namespace botw_abilities::champions {
enum class FatalSource { Native, OneShot, Mipha };
struct FatalResult {
    std::int32_t health = 0;
    FatalSource source = FatalSource::Native;
    Refusal refusal = Refusal::None;
    HealingError healing = HealingError::None;
    bool charged = false;
};

template <class Native, class Protected, class Prepare, class Used>
FatalResult healFatal(SavedState& state, const Context& context, Native native,
                      Protected protectedHit, Prepare prepare, Used used,
                      std::uint64_t expectedGeneration = UINT64_MAX) {
    const auto snapshot = state.snapshot();
    if (expectedGeneration != UINT64_MAX && snapshot.generation != expectedGeneration)
        return {native(), FatalSource::Native, Refusal::Paused};
    const auto refusal = snapshot.ready ? admission(snapshot.states[0], context) : Refusal::Paused;
    if (refusal != Refusal::None) return {native(), FatalSource::Native, refusal};
    if (protectedHit()) return {1, FatalSource::OneShot};
    const auto claim = state.reserve(snapshot.generation, Blessing::Mipha, context);
    if (!claim) return {native(), FatalSource::Native, claim.use.refusal};
    const auto healing = prepare();
    const bool charged = state.finish(claim, bool(healing));
    if (!healing) return {native(), FatalSource::Native, Refusal::None, healing.error};
    if (charged) used(claim.generation);
    return {healing.health, FatalSource::Mipha, Refusal::None, HealingError::None, charged};
}
} // namespace botw_abilities::champions
