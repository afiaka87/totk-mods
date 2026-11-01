// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "engine/Game.hpp"

#include <cmath>
#include <cstring>

namespace descend::engine {

pure::Plan makePlan(std::uintptr_t actor, Vec3 feet, unsigned rayLimit) {
    unsigned casts = 0;
    bool exhausted = false;
    auto plan = pure::plan(feet,
        [&](Vec3 from, Vec3 to, pure::Faces faces, pure::Layer layer) {
            if (rayLimit && ++casts > rayLimit) {
                exhausted = true;
                return pure::CastResult{};
            }
            return raycast(actor, from, to, faces, layer);
        }, [](Vec3 start) { return voxelUp(start); }, rayLimit == 0);
    pure::confirmExit(plan);
    return exhausted ? pure::Plan{} : plan;
}
namespace {

// The game returns this record through x8; the user-provided destructor makes the compiler do the same.
struct SystemGroup {
    std::uint32_t id = 0;
    std::uint8_t flag = 0;
    std::uint8_t padding[3]{};
    ~SystemGroup() {}
};
static_assert(sizeof(SystemGroup) == 8);

using SystemGroupFn = SystemGroup (*)(std::uintptr_t actor);
using RaycastFn = bool (*)(const Vec3* from, const Vec3* to, void* query, const SystemGroup* group,
                           std::uint32_t layer, std::uint32_t reserved);

constexpr std::size_t kQuerySize = 0x200;
constexpr std::size_t kFilter = 0x128;
// castImpl ORs 1 or 2 from +0x84 into the physics engine's ray flags; other values are ignored.
constexpr std::size_t kFaceFilter = 0x84;
constexpr std::size_t kHitPosition = 0x24, kHitNormal = 0x30;

template <class Value>
void put(unsigned char* query, std::size_t offset, Value value) {
    std::memcpy(query + offset, &value, sizeof(value));
}

} // namespace

void ensureAnimation(std::uintptr_t player, std::uintptr_t commandSlot) {
    using Check = std::uint64_t (*)(std::uintptr_t, std::uintptr_t);
    using Play = std::uint64_t (*)(std::uintptr_t, std::uint32_t, std::uintptr_t, std::uint8_t, std::uint8_t);
    const auto command = mainBase + commandSlot;
    if (!reinterpret_cast<Check>(mainBase + offsets::kCheckCommand)(player, command))
        reinterpret_cast<Play>(mainBase + offsets::kPlayCommand)(player, 0, command, 15, 0);
}

// Builds the query exactly as the game's own floor snap does before the same call.
pure::CastResult raycast(std::uintptr_t actor, Vec3 from, Vec3 to, pure::Faces faces, pure::Layer layer) {
    alignas(16) unsigned char query[kQuerySize]{};
    const auto queryVtable = readMemory<std::uintptr_t>(mainBase + offsets::kRayQueryVtableSlot);
    const auto filterVtable = readMemory<std::uintptr_t>(mainBase + offsets::kRayFilterVtableSlot);
    put<std::uint64_t>(query, 0x00, queryVtable + 0x10);
    put<std::uint64_t>(query, 0x3C, 0xBF800000u);
    put<std::uint64_t>(query, 0x60, reinterpret_cast<std::uintptr_t>(query + kFilter));
    put<std::uint32_t>(query, kFaceFilter, static_cast<std::uint32_t>(faces));
    put<std::uint32_t>(query, 0x90, 1);
    put<std::uint32_t>(query, 0x114, 1);
    put<std::uint64_t>(query, kFilter, filterVtable + 0x10);
    put<std::uint64_t>(query, 0x130, 17);
    put<std::uint64_t>(query, 0x138, 0xFFFFFFFFu);
    put<std::uint64_t>(query, 0x140, ~0ull);
    put<std::uint64_t>(query, 0x150, 1);

    const auto groupOf = reinterpret_cast<SystemGroupFn>(mainBase + offsets::kSystemGroupId);
    const auto cast = reinterpret_cast<RaycastFn>(mainBase + offsets::kRaycast);
    const SystemGroup group = groupOf(actor);
    pure::CastResult result{};
    const auto layerId = layer == pure::Layer::Solid ? offsets::kLayerSolid : offsets::kLayerCeiling;
    result.hit = cast(&from, &to, query, &group, layerId, 0);
    if (result.hit) {
        std::memcpy(&result.position, query + kHitPosition, sizeof(Vec3));
        std::memcpy(&result.normal, query + kHitNormal, sizeof(Vec3));
    }
    return result;
}

// The routine try-locks the module and returns 0 when it is busy, before touching the output; with a unit it clears bit 0 and sets it again, then writes the cell centre. The output starts with bit 0 set and NaN heights, so the three cases can be told apart.
VoxelUp voxelUp(Vec3 start) {
    using namespace offsets::voxel_up;
    using VoxelUpFn = std::uint64_t (*)(std::uintptr_t module, void* out, const Vec3* start);
    VoxelUp result{};
    const auto holder = readMemory<std::uintptr_t>(mainBase + offsets::kVolumeStatsSlot);
    if (!totk::engine::isPlausibleAddress(holder)) return result;
    const auto module = readMemory<std::uintptr_t>(holder);
    if (!totk::engine::isPlausibleAddress(module)) return result;
    alignas(16) unsigned char out[kSize]{};
    put<std::uint32_t>(out, kUnit, 1);
    for (std::size_t offset = kCellX; offset < kSize; offset += 4) put<float>(out, offset, NAN);
    const auto query = reinterpret_cast<VoxelUpFn>(mainBase + offsets::kVoxelUp);
    result.found = (query(module, out, &start) & 1) != 0;
    std::uint32_t unit;
    float cell;
    std::memcpy(&unit, out + kUnit, sizeof(unit));
    std::memcpy(&cell, out + kCellX, sizeof(cell));
    if (!(unit & 1))
        result.answer = VoxelUp::Answer::NoUnit;
    else if (std::isnan(cell))
        result.answer = VoxelUp::Answer::Busy;
    else
        result.answer = VoxelUp::Answer::Unit;
    std::memcpy(&result.firstSolid, out + kFirstSolid, sizeof(float));
    std::memcpy(&result.solidEnd, out + kSolidEnd, sizeof(float));
    std::memcpy(&result.nextSolid, out + kNextSolid, sizeof(float));
    return result;
}

} // namespace descend::engine
