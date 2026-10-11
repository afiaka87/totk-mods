// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "CryonisWorld.hpp"
#include "CryonisEngine.hpp"
#include <lib.hpp>
#include "Totk121Offsets.hpp"
#include "Totk121Layout.hpp"
#include <cstring>
#include <span>
namespace cryonis::engine {
namespace eng = totk::engine;
std::uintptr_t moduleBase{};
Vec3 position(Handle actor) { return read<Vec3>(actor.address + eng::layout::kActorPosition); }
Water sample(WorldContext world, float x, float z) {
    if (!std::isfinite(x) || !std::isfinite(z))
        return {};
    if (!world.terrain) {
        const auto holder = read<std::uintptr_t>(engine::moduleBase + offsets::kTerrainModuleSlot);
        const auto module = valid(holder) ? read<std::uintptr_t>(holder) : 0;
        if (!valid(module))
            return {};
        const char *name = "MainField";
        world.terrain =
            call<void *(*)(void *, const char **)>(offsets::kTerrainModuleSearchTerrainSceneByName)(
                reinterpret_cast<void *>(module), &name);
        if (!valid(reinterpret_cast<std::uintptr_t>(world.terrain))) {
            const auto count = read<unsigned>(module + 704);
            const auto list = read<std::uintptr_t>(module + 712);
            if (count > 0 && count <= 256 && valid(list))
                world.terrain = reinterpret_cast<void *>(read<std::uintptr_t>(list));
        }
        if (!valid(reinterpret_cast<std::uintptr_t>(world.terrain))) {
            world.terrain = nullptr;
            return {};
        }
    }
    float heights[2]{};
    const float xz[]{x, z};
    const int bottom =
        call<int (*)(float *, const float *, void *, int, unsigned, unsigned, unsigned)>(
            offsets::kSceneGetGroundHeight)(heights + 1, xz, world.terrain, -1, 0, 1, 0);
    const int code = call<int (*)(float *, const float *, void *, int)>(
        offsets::kSceneGetWaterDepth)(heights, xz, world.terrain, -1);
    if ((code != 1 && code != 2) || !std::isfinite(heights[0]) || !std::isfinite(heights[1]) ||
        std::fabs(heights[0]) > 10000 || std::fabs(heights[1]) > 10000 || heights[0] < heights[1])
        return {};
    if (bottom != 1 && bottom != 2)
        return {};
    return {heights[0], heights[1], true};
}

void scanWater(WorldContext world) {
    const unsigned back = 1 - world.water.front;
    if (!world.water.cursor) {
        const auto p = position(world.player);
        world.water.origins[back] = {std::floor(p.x * .5f) * 2.0f, water::kHighlightReach,
                                     std::floor(p.z * .5f) * 2.0f};
    }
    for (unsigned count = 0; count < 256 && world.water.cursor < kWaterSide * kWaterSide;
         ++count, ++world.water.cursor) {
        const unsigned x = world.water.cursor % kWaterSide, z = world.water.cursor / kWaterSide;
        const auto origin = world.water.origins[back];
        const auto water = sample(world, origin.x + water::coordinate(x, origin.y),
                                  origin.z + water::coordinate(z, origin.y));
        world.water.heights[back][world.water.cursor] =
            water.present && water.surface - water.bottom > 0.05f ? water.surface : kNoWater;
    }
    if (world.water.cursor == kWaterSide * kWaterSide) {
        world.water.front = back;
        world.water.cursor = 0;
    }
}

void roster(WorldContext world) {
    world.roster.candidateCount = 0;
    world.roster.count = 0;
    for (auto &group : std::span<Riders>(world.riders, kPillarLimit))
        for (unsigned i = 0; i < group.count; ++i)
            group.items[i].seen = false;
    for (unsigned i = 0; i < world.capturedRiders.count; ++i)
        world.capturedRiders.items[i].seen = false;
    const auto access = eng::resolveActiveProcessRoster(engine::moduleBase, world.scene.token);
    if (!access) {
        Logging.Log("[cryonis] ROSTER_REFUSED error=%u", unsigned(access.error));
        return;
    }
    const auto walk =
        eng::visitActiveProcesses(access.value, [&](const eng::ActiveProcessView &actor) {
            if (world.roster.count < 4096)
                world.roster.actors[world.roster.count++] = actor.handle;
            const auto mark = [&](Riders &group) {
                for (unsigned i = 0; i < group.count; ++i) {
                    auto &rider = group.items[i];
                    if (rider.actor.address == actor.handle.address &&
                        rider.actor.capturedNamePointer == actor.handle.capturedNamePointer)
                        rider.seen = true;
                }
            };
            for (auto &group : std::span<Riders>(world.riders, kPillarLimit))
                mark(group);
            mark(world.capturedRiders);
            if (actor.processName.equals(kActor) && world.roster.candidateCount < 8)
                world.roster.candidates[world.roster.candidateCount++] = actor.handle;
            return eng::VisitControl::Continue;
        });
    if (!walk)
        Logging.Log("[cryonis] ROSTER_WALK_REFUSED error=%u", unsigned(walk.error));
}

bool seen(WorldContext world, Handle h) {
    if (!h.address || !h.isCurrent(world.scene.token))
        return false;
    for (unsigned i = 0; i < world.roster.candidateCount; ++i)
        if (world.roster.candidates[i].address == h.address &&
            world.roster.candidates[i].capturedNamePointer == h.capturedNamePointer)
            return true;
    return false;
}

std::uintptr_t body(Handle h) {
    const auto registry = read<std::uintptr_t>(h.address + layout::actor::kComponents);
    const auto physics =
        valid(registry) ? read<std::uintptr_t>(registry + layout::actor::kPhysics) : 0;
    const auto set =
        valid(physics) ? read<std::uintptr_t>(physics + layout::actor::kControllerSet) : 0;
    return valid(set) ? read<std::uintptr_t>(set + 0x150) : 0;
}

bool shadowOnly(Handle actor) {
    const auto controller =
        call<std::uintptr_t (*)(std::uintptr_t)>(offsets::kGetModelController)(actor.address);
    const auto model =
        call<std::uintptr_t (*)(std::uintptr_t)>(offsets::kActorBaseGetModel)(actor.address);
    if (!valid(controller) || !valid(model))
        return false;
    call<void (*)(std::uintptr_t, bool)>(offsets::kGameModelControllerEnableDepthShadowOnly)(
        controller, true);
    // Apply immediately as well as dirtying the controller's persistent setting.
    call<void (*)(std::uintptr_t, bool, int)>(offsets::kSetModelDepthShadowOnly)(model, true, -1);
    return true;
}

bool riderEligible(WorldContext world, Handle actor) {
    if (actor.address == world.player.address)
        return true;
    const char *name = reinterpret_cast<const char *>(actor.capturedNamePointer);
    if (std::strcmp(name, kActor) == 0)
        return false;
    const auto equipment =
        call<std::uintptr_t (*)(std::uintptr_t)>(offsets::kGetDynamicEquipment)(actor.address);
    if (valid(equipment) &&
        call<std::uintptr_t (*)(std::uintptr_t)>(offsets::kDynamicEquipmentGetUser)(equipment))
        return false;
    const auto b = body(actor);
    if (!valid(b))
        return false;
    return call<unsigned (*)(std::uintptr_t)>(offsets::kRigidBodyBaseGetMotionType)(b) == 2 ||
           std::strncmp(name, "Animal_", 7) == 0 || std::strncmp(name, "Enemy_", 6) == 0 ||
           std::strncmp(name, "Npc_", 4) == 0;
}

Riders captureRiders(WorldContext world, Vec3 base, float yaw) {
    Riders group{};
    unsigned overflow{};
    // This runs once per accepted construction, outside the process-list lock.
    for (unsigned i = 0; i < world.roster.count; ++i) {
        const auto actor = world.roster.actors[i];
        if (!actor.isCurrent(world.scene.token) || !riderEligible(world, actor))
            continue;
        const auto b = body(actor);
        lift::Bounds bounds{};
        if (valid(b))
            call<void (*)(std::uintptr_t, lift::Bounds *)>(
                offsets::kRigidBodyBaseGetBoundingBoxWorld)(b, &bounds);
        if (!lift::capture(base, bounds, yaw))
            continue;
        if (group.count == kRiderLimit) {
            ++overflow;
            continue;
        }
        const auto p = position(actor);
        if (!finite(p))
            continue;
        const lift::Bounds relative{sub(bounds.min, p), sub(bounds.max, p)};
        group.items[group.count++] = {actor, relative, {}, true, false};
        Logging.Log("[cryonis] RIDER_CAPTURE name=%s bottom=%f top=%f water=%f",
                    reinterpret_cast<const char *>(actor.capturedNamePointer), double(bounds.min.y),
                    double(bounds.max.y), double(base.y));
    }
    Logging.Log("[cryonis] RIDERS count=%u overflow=%u", group.count, overflow);
    return group;
}

void liftRiders(WorldContext world, Riders &group, Vec3 base, float frame, bool finished,
                float yaw) {
    for (unsigned i = 0; i < group.count; ++i) {
        auto &rider = group.items[i];
        if (!rider.actor.address)
            continue;
        if (!rider.seen || !rider.actor.isCurrent(world.scene.token)) {
            rider.actor = {};
            continue;
        }
        auto p = position(rider.actor);
        if (!finite(p)) {
            rider.actor = {};
            continue;
        }
        // Account for a queued transform that has not reached the actor yet.
        if (rider.hasRequest && std::fabs(p.x - rider.requested.x) < .1f &&
            std::fabs(p.z - rider.requested.z) < .1f)
            p.y = std::max(p.y, rider.requested.y);
        auto bounds = lift::translated(rider.relative, p);
        if (!lift::footprint(base, bounds, yaw)) {
            rider.actor = {};
            continue;
        }
        const float delta = lift::rise(base, frame, bounds, yaw);
        if (delta > 0) {
            p.y += delta;
            float rotation[9]{};
            std::memcpy(
                rotation,
                reinterpret_cast<const void *>(rider.actor.address + layout::actor::kRotation),
                sizeof(rotation));
            bool okay = true;
            for (float value : rotation)
                okay &= std::isfinite(value);
            if (!okay) {
                rider.actor = {};
                continue;
            }
            const float matrix[]{rotation[0], rotation[1], rotation[2], p.x,
                                 rotation[3], rotation[4], rotation[5], p.y,
                                 rotation[6], rotation[7], rotation[8], p.z};
            call<void (*)(void *, const float *, unsigned)>(offsets::kActorBaseForceSetMatrix)(
                reinterpret_cast<void *>(rider.actor.address), matrix, 0);
            const auto b = body(rider.actor);
            if (valid(b)) {
                const auto velocity = call<std::uintptr_t (*)(std::uintptr_t)>(
                    offsets::kRigidBodyEntityGetLinearVelocity)(b);
                if (valid(velocity)) {
                    auto v = read<Vec3>(velocity);
                    if (finite(v) && v.y < 0) {
                        v.y = 0;
                        call<void (*)(std::uintptr_t, const Vec3 *)>(
                            offsets::kRigidBodyBaseRequestSetLinearVelocity)(b, &v);
                    }
                }
            }
            rider.requested = p;
            rider.hasRequest = true;
        }
    }
    if (finished) {
        for (unsigned i = 0; i < group.count; ++i)
            if (group.items[i].actor.address) {
                const auto &rider = group.items[i];
                const auto p = position(rider.actor);
                Logging.Log(
                    "[cryonis] RIDER_END name=%s actual_y=%f requested_y=%f expected_bottom=%f",
                    reinterpret_cast<const char *>(rider.actor.capturedNamePointer), double(p.y),
                    double(rider.requested.y),
                    double(base.y + kHeight - kSettledSubmerge + kRiderClearance));
            }
        Logging.Log("[cryonis] RIDERS_RELEASE count=%u", group.count);
        group = {};
    }
}

void move(Handle h, Vec3 base, float frame, float yaw) {
    // Keep unit scale to avoid convex-radius clamping; growth raises the submerged body.
    const auto p = collisionBase(base, frame);
    const float c = std::cos(yaw), s = std::sin(yaw);
    const float rows[]{c, 0, s, p.x, 0, 1, 0, p.y, -s, 0, c, p.z};
    call<void (*)(void *, const float *, unsigned)>(offsets::kActorBaseForceSetMatrix)(
        reinterpret_cast<void *>(h.address), rows, 0);
    const auto b = body(h);
    const float zero[3]{};
    if (valid(b))
        call<void (*)(void *, const float *)>(offsets::kRigidBodyBaseRequestSetLinearVelocity)(
            reinterpret_cast<void *>(b), zero);
}

void modelInitialized(std::uintptr_t component) {
    if (!valid(component))
        return;
    const auto actor = read<std::uintptr_t>(component + 24);
    if (!valid(actor))
        return;
    const auto name = read<std::uintptr_t>(actor + eng::layout::kActorNamePointer);
    if (!eng::isPlausibleStringAddress(name) ||
        std::strcmp(reinterpret_cast<const char *>(name), kActor) != 0)
        return;
    const bool hidden = shadowOnly({actor, name, {}});
    Logging.Log("[cryonis] SHADOW_INITIALIZED hidden=%u", unsigned(hidden));
}

bool isSwimming(Handle actor) {
    return call<bool (*)(std::uintptr_t)>(offsets::kGameActorIsSwimming)(actor.address);
}
void deleteActor(Handle actor) {
    call<void (*)(void *)>(offsets::kBaseProcRequestDelete)(
        reinterpret_cast<void *>(actor.address));
}
void fixBody(std::uintptr_t body) {
    call<void (*)(void *, unsigned)>(offsets::kRigidBodyBaseRequestChangeMotionType)(
        reinterpret_cast<void *>(body), 1);
}
target::Group collisionGroup(Handle actor) {
    return call<target::Group (*)(const std::uintptr_t *)>(
        offsets::kActorReferenceGetSystemGroupID)(&actor.address);
}
bool spawnPillar(const Target &target) {
    struct Argument {
        float position[3], rotation[9], scale[3];
        const char *name;
        unsigned char opaque48[0x10];
        void *blackboard;
        void *opaque60;
        void *parent;
        void *dependent;
        void *creator;
        void *rtti;
        void *heap;
        unsigned char flags;
        unsigned char padding[7];
    } argument{};
    static_assert(sizeof(Argument) == 0x98);
    const auto manager = read<void *>(engine::moduleBase + offsets::kActorManager);
    if (!manager) {
        Logging.Log("[cryonis] SPAWN_REFUSED manager unavailable");
        return false;
    }
    const auto start = collisionBase(target.base, 0);
    argument.position[0] = start.x;
    argument.position[1] = start.y;
    argument.position[2] = start.z;
    const float c = std::cos(target.yaw), s = std::sin(target.yaw);
    argument.rotation[0] = argument.rotation[8] = c;
    argument.rotation[4] = 1;
    argument.rotation[2] = s;
    argument.rotation[6] = -s;
    for (float &scale : argument.scale)
        scale = 1;
    argument.name = "";
    argument.flags = 7;
    const char *actor = kActor;
    unsigned result{};
    void *preactor{};
    const bool accepted = call<bool (*)(void *, const void *, const Argument *, void *, unsigned,
                                        void *, void *, void *, bool, unsigned *, void **)>(
        offsets::kActorMgrRequestCreateActorAsync)(manager, &actor, &argument, nullptr, 1, nullptr,
                                                   nullptr, nullptr, false, &result, &preactor);
    Logging.Log("[cryonis] SPAWN accepted=%u result=%u preactor=%p base=%f,%f,%f",
                unsigned(accepted), result, preactor, double(target.base.x), double(target.base.y),
                double(target.base.z));
    return accepted;
}
}
