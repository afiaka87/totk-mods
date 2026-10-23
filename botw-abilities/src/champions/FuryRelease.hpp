// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once
#include "SavedState.hpp"
#include "totk/core/Units.hpp"

namespace botw_abilities::champions {
struct FuryRequest {
    std::uint64_t generation = 0;
    std::uintptr_t scene = 0;
    totk::core::WorldTransform placement{};
};
struct FuryResult {
    ChargeReservation claim;
    bool spawned = false, committed = false;
    explicit operator bool() const { return spawned && committed; }
};

template<class Spawn>
FuryResult releaseFury(SavedState& saved, std::uint64_t generation, Context context, Spawn spawn) {
    const auto claim = saved.reserve(generation,Blessing::Urbosa,context);
    if (!claim) return {claim};
    const bool accepted = spawn();
    return {claim,accepted,saved.finish(claim,accepted)};
}
} // namespace botw_abilities::champions
