// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#pragma once

#include "engine/DescendOffsets.hpp"
#include "pure/DescendGeometry.hpp"
#include "totk/engine/Pointer.hpp"

#include <cstdint>

namespace descend::engine {

using pure::Vec3;
using totk::engine::readMemory;
using totk::engine::writeMemory;

inline std::uintptr_t mainBase = 0;

inline constexpr std::uint64_t kTicksPerSecond = 19'200'000;
inline constexpr std::uint64_t ticks(std::uint64_t milliseconds) {
    return kTicksPerSecond / 1000 * milliseconds;
}

// Follows one pointer field; zero when the holder or the stored value is not a plausible address.
inline std::uintptr_t follow(std::uintptr_t holder, std::uintptr_t offset) {
    if (!totk::engine::isPlausibleAddress(holder)) return 0;
    const auto value = readMemory<std::uintptr_t>(holder + offset);
    return totk::engine::isPlausibleAddress(value) ? value : 0;
}

// Actor that owns an AI node (the same chain the game's ExecuteBase::getActor walks).
inline std::uintptr_t nodeActor(const void* node) {
    using namespace offsets::node;
    return follow(follow(follow(reinterpret_cast<std::uintptr_t>(node), kContext), kContextOwner),
                  kOwnerActor);
}

inline std::uintptr_t component(std::uintptr_t actor, std::uintptr_t slot) {
    return follow(follow(actor, offsets::actor::kComponents), slot);
}

inline std::uintptr_t playerComponent(std::uintptr_t actor) {
    return component(actor, offsets::components::kPlayer);
}

inline std::uintptr_t ceilingClipper(std::uintptr_t actor) {
    return follow(component(actor, offsets::components::kSpecialPower), offsets::power::kCeilingClipper);
}

inline Vec3 readVec(std::uintptr_t address) { return readMemory<Vec3>(address); }
inline void writeVec(std::uintptr_t address, Vec3 value) { writeMemory(address, value); }

// One synchronous world raycast against one layer, ignoring the actor's own group.
pure::CastResult raycast(std::uintptr_t actor, Vec3 from, Vec3 to, pure::Faces faces,
                         pure::Layer layer = pure::Layer::Clipper);

// Ascend's voxel look upward from a point.
using pure::VoxelUp;
VoxelUp voxelUp(Vec3 start);
pure::Plan makePlan(std::uintptr_t actor, Vec3 feet, unsigned rayLimit = 0);

// Native command slots, lower-body channel zero. Do not restart an already active command.
void ensureAnimation(std::uintptr_t player, std::uintptr_t commandSlot);

} // namespace descend::engine
