// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "engine/Game.hpp"
#include <lib.hpp>

#include <cmath>
#include <cstring>

namespace descend::engine {

namespace {
struct WorldCast {
    std::uintptr_t actor;
    unsigned rayLimit = 0;
    unsigned casts = 0;
    bool exhausted = false;
    pure::ExitSurvey* survey = nullptr;
    bool reserveRay() {
        if (!rayLimit || ++casts <= rayLimit) return true;
        exhausted = true;
        return false;
    }
    pure::CastResult operator()(Vec3 from, Vec3 to, pure::Faces faces, pure::Layer layer) {
        if (survey) return survey->ray(from, to, faces, layer,
            [this](Vec3 a, Vec3 b, pure::Faces f, pure::Layer l) { return raycast(actor, a, b, f, l); });
        if (!reserveRay()) return {};
        return raycast(actor, from, to, faces, layer);
    }
    pure::BodyCastResult body(Vec3 from, Vec3 to) {
        pure::BodyCastResult result;
        if (!reserveRay()) return result;
        static_cast<pure::CastResult&>(result) = raycast(actor,from,to,pure::Faces::All,pure::Layer::Solid,&result.body);
        return result;
    }
};
struct BodyCast {
    WorldCast& world;
    pure::BodyCastResult operator()(Vec3 a, Vec3 b) const { return world.body(a,b); }
};
struct VoxelLookup {
    pure::ExitSurvey* survey = nullptr;
    pure::VoxelUp operator()(Vec3 p) const { return survey ? survey->voxel(p,voxelUp) : voxelUp(p); }
};
bool returnClear(WorldCast& cast, Vec3 feet);
}

pure::Plan makePlan(std::uintptr_t actor, Vec3 feet, unsigned rayLimit) {
    WorldCast cast{actor,rayLimit};
    VoxelLookup up;
    auto plan = pure::plan(feet, cast, up, rayLimit == 0);
    pure::confirmExit(plan);
    if (!plan.valid() && !cast.exhausted) {
        const auto floor = cast.body(pure::at(feet, feet.y + pure::tuning::kFloorAbove),
                                     pure::at(feet, feet.y - pure::tuning::kFloorBelow));
        BodySnapshot body;
        if (snapshotBody(floor.body, body) && pure::acceptsMotion(body.velocity, readVec(actor + offsets::actor::kClothWind))) {
            auto object = pure::objectPlan(feet, floor, cast, up, BodyCast{cast}, rayLimit == 0);
            if (object.valid() && !returnClear(cast, feet)) object.verdict = pure::Verdict::BlockedExit;
            plan = object;
        }
        if (plan.verdict == pure::Verdict::NoUnderside && !rayLimit)
            plan = pure::deepCandidate(feet, plan, [](Vec3 p, bool ceiling) { return voxelDown(p, ceiling); }, up);
    }
    return cast.exhausted ? pure::Plan{} : plan;
}
namespace {

std::uintptr_t physicsWorld() {
    const auto system = follow(follow(imageBase(), offsets::kPhysicsSlot), 0);
    const auto framework = follow(system, 0xC8);
    return follow(follow(framework, 0xC0), offsets::kPhysicsWorldFromWrapper);
}

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
    const auto command = imageBase() + commandSlot;
    if (!reinterpret_cast<Check>(imageBase() + offsets::kCheckCommand)(player, command))
        reinterpret_cast<Play>(imageBase() + offsets::kPlayCommand)(player, 0, command, 15, 0);
}

pure::CastResult raycast(std::uintptr_t actor, Vec3 from, Vec3 to, pure::Faces faces, pure::Layer layer, pure::BodyKey* body) {
    alignas(16) unsigned char query[kQuerySize]{};
    const auto queryVtable = offsets::kDirectRayVtables ? imageBase() + offsets::kRayQueryVtableSlot
        : readMemory<std::uintptr_t>(imageBase() + offsets::kRayQueryVtableSlot);
    const auto filterVtable = offsets::kDirectRayVtables ? imageBase() + offsets::kRayFilterVtableSlot
        : readMemory<std::uintptr_t>(imageBase() + offsets::kRayFilterVtableSlot);
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

    const auto groupOf = reinterpret_cast<SystemGroupFn>(imageBase() + offsets::kSystemGroupId);
    const auto cast = reinterpret_cast<RaycastFn>(imageBase() + offsets::kRaycast);
    const SystemGroup group = groupOf(actor);
    pure::CastResult result{};
    const auto layerId = layer == pure::Layer::Solid ? offsets::kLayerSolid : offsets::kLayerCeiling;
    result.hit = cast(&from, &to, query, &group, layerId, 0);
    if (result.hit) {
        std::memcpy(&result.position, query + kHitPosition, sizeof(Vec3));
        std::memcpy(&result.normal, query + kHitNormal, sizeof(Vec3));
        if (body) {
            std::memcpy(&body->id, query + 0x120, sizeof(body->id));
            std::memcpy(&body->body, query + 0x170, sizeof(body->body));
            body->world = physicsWorld();
        }
    }
    return result;
}

bool snapshotBody(pure::BodyKey key, BodySnapshot& snapshot) {
    if (!key || key.world != physicsWorld()) return false;
    using Valid = bool (*)(std::uintptr_t, std::uint64_t);
    using Get = std::uintptr_t (*)(std::uintptr_t, std::uint64_t);
    if (!reinterpret_cast<Valid>(imageBase() + offsets::kBodyValid)(key.world, key.id)) return false;
    const auto native = reinterpret_cast<Get>(imageBase() + offsets::kGetBody)(key.world, key.id);
    if (!totk::engine::isPlausibleAddress(native) || readMemory<std::uintptr_t>(native + 0xB8) != key.body)
        return false;
    const auto sdk = follow(key.body, 0x70);
    if (!sdk || readMemory<std::uint64_t>(sdk + 8) != key.id) return false;
    // Terrain still needs its voxel confirmation; only movable/keyframed bodies use this lane.
    if constexpr (offsets::kBodyMotion) {
        using Motion = unsigned (*)(std::uintptr_t);
        if (!reinterpret_cast<Motion>(imageBase() + offsets::kBodyMotion)(key.body)) return false;
    } else {
        // The native helper is folded into its callers on 1.4.x. The verified pending-request flag takes precedence over the body's current flags.
        const auto pending = follow(key.body, 0x60);
        if (pending && (readMemory<std::uint32_t>(pending + 0xD4) & 4)) {
            if (!readMemory<std::uint32_t>(pending + 0x80)) return false;
        } else if (readMemory<std::uint64_t>(key.body + 0x68) & 0x40) {
            return false;
        }
    }
    using Matrix = void (*)(std::uintptr_t, pure::Matrix34*);
    reinterpret_cast<Matrix>(imageBase() + offsets::kBodyMatrix)(key.body, &snapshot.matrix);
    snapshot.velocity = readVec(key.body + 0xF8);
    return snapshot.matrix.valid() && pure::finitePoint(snapshot.velocity);
}

pure::Plan movingPlan(std::uintptr_t actor, pure::BodyKey key, Vec3 localOrigin) {
    BodySnapshot body;
    if (!snapshotBody(key, body)) return {};
    const auto origin = body.matrix.point(localOrigin);
    WorldCast cast{actor};
    const auto floor = cast.body(pure::at(origin, origin.y + pure::tuning::kFloorAbove),
                                 pure::at(origin, origin.y - pure::tuning::kFloorBelow));
    if (!(floor.body == key)) return {};
    VoxelLookup up;
    return pure::objectPlan(origin, floor, cast, up, BodyCast{cast}, false);
}

pure::Plan validateDeepExit(std::uintptr_t actor, const pure::Plan& candidate, unsigned serial) {
    static pure::ExitSurvey survey;
    static unsigned surveySerial;
    static std::uintptr_t surveyActor;
    static std::uint64_t surveyTick;
    WorldCast cast{actor};
    VoxelLookup up;
    if (serial) {
        const auto tick = svcGetSystemTick();
        if (serial != surveySerial || actor != surveyActor || tick - surveyTick > ticks(pure::tuning::kStaleMs)) {
            survey.count = survey.voxelCount = 0;
            surveySerial = serial;
            surveyActor = actor;
        }
        surveyTick = tick;
        survey.begin();
        cast.survey = up.survey = &survey;
    } else {
        surveySerial = 0;
    }
    const auto result = pure::loadedDeepExit(candidate, cast, up, serial == 0);
    return serial ? survey.finish(result) : result;
}

bool preloadReady(Vec3 target) {
    using Ready = bool (*)(bool, const Vec3*);
    return reinterpret_cast<Ready>(imageBase() + offsets::kPreloadReady)(false, &target);
}

bool depthsAt(Vec3 position) {
    const auto module = offsets::kWorldDirect ? follow(imageBase(), offsets::kWorldSlot)
        : follow(follow(imageBase(), offsets::kWorldSlot), 0);
    const auto manager = follow(module, 0xD0);
    if (!manager || !pure::finitePoint(position)) return false;
    const auto table = follow(manager, 0);
    const auto method = table ? readMemory<std::uintptr_t>(table + offsets::kWorldPositionMethod) : 0;
    return method && reinterpret_cast<float (*)(std::uintptr_t, const Vec3*)>(method)(manager, &position) >= 1.0f;
}

void stopTravel(std::uintptr_t player) {
    const Vec3 zero{};
    using Set = void (*)(std::uintptr_t, const Vec3*, float);
    reinterpret_cast<Set>(imageBase() + offsets::kSetForwardSpeed)(player, &zero, 0.0f);
}

bool moveEventSupport(std::uintptr_t actor, Vec3 target) {
    if (!pure::finitePoint(target)) return false;
    using Component = std::uintptr_t (*)(std::uintptr_t);
    const auto physics = reinterpret_cast<Component>(imageBase() + offsets::kPhysicsComponent)(actor);
    const auto controllers = follow(physics, 0x20);
    if (!controllers) return false;
    std::uintptr_t body;
    if constexpr (offsets::kSplitNamedBody) {
        using Find = std::uintptr_t (*)(std::uintptr_t, std::uintptr_t);
        const auto category = reinterpret_cast<Find>(imageBase() + offsets::kFindBodyCategory)(
            controllers, imageBase() + offsets::kBodyCategorySlot);
        if (!totk::engine::isPlausibleAddress(category)) return false;
        body = reinterpret_cast<Find>(imageBase() + offsets::kFindBodyInCategory)(
            category, imageBase() + offsets::kBodyNameSlot);
    } else {
        using Find = std::uintptr_t (*)(std::uintptr_t, std::uintptr_t, std::uintptr_t);
        body = reinterpret_cast<Find>(imageBase() + offsets::kNamedBody)(controllers,
            readMemory<std::uintptr_t>(imageBase() + offsets::kBodyCategorySlot),
            readMemory<std::uintptr_t>(imageBase() + offsets::kBodyNameSlot));
    }
    if (!totk::engine::isPlausibleAddress(body)) return false;
    pure::Matrix34 matrix;
    matrix.m[0][3] = target.x;
    matrix.m[1][3] = target.y + readMemory<float>(imageBase() + offsets::kSupportLift);
    matrix.m[2][3] = target.z;
    using Set = void (*)(std::uintptr_t, const pure::Matrix34*);
    reinterpret_cast<Set>(imageBase() + offsets::kSetBodyMatrix)(body, &matrix);
    return true;
}

namespace {
bool returnClear(WorldCast& cast, Vec3 feet) {
    if (!pure::finitePoint(feet)) return false;
    const float top = feet.y + pure::tuning::kBodyBottom + 0.05f;
    if (pure::bodyBlocked(feet, top, cast) >= 0 ||
        pure::bodyBlocked(feet, top + pure::tuning::kReturnLift, cast) >= 0) return false;
    for (auto layer : {pure::Layer::Clipper, pure::Layer::Solid}) {
        if (cast(pure::at(feet,feet.y+0.05f), pure::at(feet,top), pure::Faces::All, layer).hit ||
            pure::rockVotes(pure::at(feet,feet.y+1.0f),cast,layer) >= pure::tuning::kRockVotes) return false;
    }
    return true;
}
}

bool clearReturn(std::uintptr_t actor, Vec3 feet) {
    WorldCast cast{actor};
    return returnClear(cast, feet);
}

pure::VoxelSurface voxelDown(Vec3 start, bool ceiling) {
    const auto module = follow(follow(imageBase(), offsets::kVolumeStatsSlot), 0);
    if (!module || !pure::finitePoint(start)) return {};
    alignas(16) unsigned char out[0x40]{};
    put<float>(out, 8, NAN);
    using Query = bool (*)(std::uintptr_t, void*, const Vec3*, bool, bool);
    const bool found = reinterpret_cast<Query>(imageBase() + offsets::kVoxelVertical)(module, out, &start, false, ceiling);
    float height;
    std::memcpy(&height, out + 8, sizeof(height));
    return {found, height};
}

// The routine try-locks the module and returns 0 when it is busy, before touching the output; with a unit it clears bit 0 and sets it again, then writes the cell centre. The output starts with bit 0 set and NaN heights, so the three cases can be told apart.
VoxelUp voxelUp(Vec3 start) {
    using namespace offsets::voxel_up;
    using VoxelUpFn = std::uint64_t (*)(std::uintptr_t module, void* out, const Vec3* start);
    VoxelUp result{};
    const auto holder = readMemory<std::uintptr_t>(imageBase() + offsets::kVolumeStatsSlot);
    if (!totk::engine::isPlausibleAddress(holder)) return result;
    const auto module = readMemory<std::uintptr_t>(holder);
    if (!totk::engine::isPlausibleAddress(module)) return result;
    alignas(16) unsigned char out[kSize]{};
    put<std::uint32_t>(out, kUnit, 1);
    for (std::size_t offset = kCellX; offset < kSize; offset += 4) put<float>(out, offset, NAN);
    const auto query = reinterpret_cast<VoxelUpFn>(imageBase() + offsets::kVoxelUp);
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
