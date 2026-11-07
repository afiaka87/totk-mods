// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "Awareness.hpp"
#include "champions/FuryBalance.hpp"
#include "champions/Runtime.hpp"
#include "NativeApi.hpp"
#include "WorldMath.hpp"
#include "totk/engine/ActorRoster.hpp"
#include <lib.hpp>
#include <atomic>
#include <cmath>
#include <iterator>

namespace botw_abilities::wolf {
namespace {
using namespace totk::engine;
using totk::core::WorldPosition;
constexpr CodeSite kSites[]{
    {0x1327B90, 0xF9411408, 0xB4000168}, {0x83AA68, 0xD10143FF, 0xA9017BFD},
};
constexpr float kBattleRange = 45, kDropRange = 100, kFoodRange = 30, kDropGap = 6, kChainRange = 20;
constexpr float kSubmerged = 1.25f, kStartleRange = 30;
constexpr std::uintptr_t kMessageLink = 184, kMessageId = 20;
constexpr std::uintptr_t kTerrainSlot = 0x462E178, kTerrainByName = 0x1334BAC,
                         kGroundHeight = 0x7FE7C0, kWaterDepth = 0xCE8C94, kSetMatrix = 0x6BA86C;
NativeApi g_api;
std::atomic_flag g_lock{};
Awareness g_view;
struct Restriction { std::uint32_t id = UINT32_MAX; std::uint64_t until{}; };
Restriction g_targets[8], g_foods[8];
unsigned g_nextTarget{}, g_nextFood{};
std::uint32_t g_seen[32]{};
unsigned g_nextSeen{};
std::uint32_t g_avoid = UINT32_MAX, g_consume = UINT32_MAX;
void* g_terrain{};

struct Guard {
    Guard() { while (g_lock.test_and_set(std::memory_order_acquire)) {} }
    ~Guard() { g_lock.clear(std::memory_order_release); }
};

std::uint64_t now() { return champions::simulationTick() >> 32; }
bool restricted(const Restriction (&list)[8], std::uint32_t id, std::uint64_t time) {
    for (const auto& entry : list)
        if (entry.id == id && time < entry.until) return true;
    return false;
}
std::uintptr_t lifeHolder(std::uintptr_t actor) {
    return pointerAt(actorComponent(actor, 0x2F8), 0x1830);
}
bool alive(std::uintptr_t actor) {
    const auto holder = lifeHolder(actor);
    return holder && readMemory<int>(holder + 8) > 0;
}
bool firstSeen(std::uint32_t id) {
    for (const auto seen : g_seen)
        if (seen == id) return false;
    g_seen[g_nextSeen++ % 32] = id;
    return true;
}
void* terrain() {
    if (g_terrain) return g_terrain;
    const auto module = g_api.singleton(kTerrainSlot);
    if (!module) return nullptr;
    const char* name = "MainField";
    auto* scene = g_api.at<void*(*)(void*, const char**)>(kTerrainByName)(reinterpret_cast<void*>(module), &name);
    g_terrain = isPlausibleAddress(reinterpret_cast<std::uintptr_t>(scene)) ? scene : nullptr;
    return g_terrain;
}
// The ground test keeps caves and the Depths, far below the map's water, from reading as submerged.
bool submerged(const WorldPosition& position) {
    auto* scene = terrain();
    if (!scene) return false;
    float surface[2]{}, ground{};
    const float xz[]{position.x, position.z};
    const int groundCode = g_api.at<int(*)(float*, const float*, void*, int, unsigned, unsigned, unsigned)>(
        kGroundHeight)(&ground, xz, scene, -1, 0, 1, 0);
    const int code = g_api.at<int(*)(float*, const float*, void*, int)>(kWaterDepth)(surface, xz, scene, -1);
    if ((code != 1 && code != 2) || (groundCode != 1 && groundCode != 2)) return false;
    if (!std::isfinite(surface[0]) || !std::isfinite(ground)) return false;
    return position.y >= ground - 1 && surface[0] - position.y > kSubmerged;
}

// One scan's fixed inputs and the best choices so far.
struct Scan {
    const WorldPosition& wolf;
    const WorldPosition& player;
    std::uint32_t wolfId, current, avoid, consume;
    std::uint64_t time;
    Awareness next{};
    float bestTarget = 1e9f, bestFood = 1e9f;
    std::uintptr_t eaten{};
};

struct Candidate {
    std::uintptr_t address;
    std::uint32_t id;
    WorldPosition position;
    std::string_view name;
    float distance;
    bool held;
};

void considerFood(Scan& scan, const Candidate& actor) {
    const int recover = foodRecover(actor.name);
    if (!recover) return;
    const bool fresh = firstSeen(actor.id);
    if (actor.distance < scan.bestFood && actor.distance <= kFoodRange && !restricted(g_foods, actor.id, scan.time)) {
        scan.bestFood = actor.distance;
        // Item actors do not expose BotW's player-put flag; a food appearing beside the player was dropped.
        const bool dropped = fresh && flatDistance(actor.position, scan.player) <= kDropGap;
        scan.next.food = {actor.address, actor.id, actor.position, true, dropped, recover};
    }
}

void noteStartled(Awareness& next, std::uintptr_t address) {
    const auto link = readMemory<std::uintptr_t>(address + kMessageLink);
    const auto message = isPlausibleAddress(link) ? readMemory<std::uint32_t>(link + kMessageId) : UINT32_MAX;
    if (message != UINT32_MAX) next.nearby[next.nearbyCount++] = message;
}

void considerTarget(Scan& scan, const Candidate& actor) {
    const bool enemy = champions::furyBalance(actor.name).has_value();
    const bool prey = !enemy && isPrey(actor.name);
    if (prey && actor.distance <= kStartleRange && scan.next.nearbyCount < std::size(scan.next.nearby))
        noteStartled(scan.next, actor.address);
    if ((!enemy && !prey) || restricted(g_targets, actor.id, scan.time) || !alive(actor.address)) return;
    if (enemy && !g_api.at<bool(*)(std::uintptr_t)>(0x1327B90)(actor.address)) return;
    // Hostile enemies come before prey, the current target before others, the nearest last.
    float score = actor.distance + (prey ? 1000.0f : 0.0f);
    if (actor.held && scan.avoid == UINT32_MAX) score -= 500;
    if (actor.id == scan.avoid) score += actor.distance <= kChainRange ? 500 : 2000;
    if (score < scan.bestTarget) {
        scan.bestTarget = score;
        scan.next.target = {actor.address, actor.id, actor.position, true, prey, 0};
    }
}

void consider(Scan& scan, const ActiveProcessView& process) {
    if (process.processState != 4) return;
    const auto address = process.handle.address;
    const auto id = readMemory<std::uint32_t>(address + 0x10);
    if (id == scan.wolfId) return;
    if (id == scan.consume) {
        scan.eaten = address;
        return;
    }
    const std::string_view name(process.processName.c_str());
    const auto position = readMemory<WorldPosition>(address + layout::kActorPosition);
    const float distance = flatDistance(position, scan.wolf);
    const bool held = id == scan.current;
    if (distance > (held ? kDropRange : kBattleRange)) return;
    const Candidate actor{address, id, position, name, distance, held};
    if (name.starts_with("Item_")) considerFood(scan, actor);
    else considerTarget(scan, actor);
}
}

bool initializeAwarenessApi(std::uintptr_t base) { return g_api.bind(base, kSites, "AWARENESS_ABI_REFUSED"); }

void scanSurroundings(const SceneContext& scene, std::uint32_t wolfId, const WorldPosition& wolf, const WorldPosition& player) {
    const auto access = resolveActiveProcessRoster(g_api.base(), scene.token);
    if (!access) return;
    const auto time = now();
    std::uint32_t current, avoid, consume;
    {
        Guard guard;
        current = g_view.target.id;
        avoid = g_avoid;
        consume = g_consume;
    }
    Scan scan{wolf, player, wolfId, current, avoid, consume, time};
    const auto walk = visitActiveProcesses(access.value, [&](const ActiveProcessView& process) {
        consider(scan, process);
        return VisitControl::Continue;
    });
    if (!walk) return;
    // BotW's Wolf Link does not swim: when the player is in deep water he warps to the player rather than
    // pacing the shore (the earlier build only tested the wolf, who never enters water, so the warp never fired).
    scan.next.deepWater = submerged(player) || submerged(wolf);
    if (scan.eaten) g_api.at<bool(*)(std::uintptr_t, unsigned)>(0x83AA68)(scan.eaten, 0);
    Guard guard;
    if (scan.eaten && g_consume == consume) g_consume = UINT32_MAX;
    g_view = scan.next;
}

Awareness surroundings() {
    Guard guard;
    return g_view;
}

void restrictTarget(std::uint32_t id, float ticks) {
    Guard guard;
    g_targets[g_nextTarget++ % 8] = {id, now() + std::uint64_t(ticks)};
    if (g_view.target.id == id) g_view.target = {};
}

void avoidTarget(std::uint32_t id) {
    Guard guard;
    g_avoid = id;
}

void restrictFood(std::uint32_t id) {
    Guard guard;
    g_foods[g_nextFood++ % 8] = {id, now() + 300};
    if (g_view.food.id == id) g_view.food = {};
}

void consumeFood(std::uint32_t id) {
    Guard guard;
    g_consume = id;
    if (g_view.food.id == id) g_view.food = {};
}

void clearSurroundings() {
    Guard guard;
    g_view = {};
    g_avoid = g_consume = UINT32_MAX;
    g_terrain = nullptr;
}

std::optional<int> readLife(const ActorReference& actor) {
    if (!actor.address()) return std::nullopt;
    const auto holder = lifeHolder(actor.address());
    if (!holder) return std::nullopt;
    return readMemory<int>(holder + 8);
}

void writeLife(const ActorReference& actor, int life) {
    if (const auto holder = actor.address() ? lifeHolder(actor.address()) : 0) writeMemory(holder + 8, life);
}

bool moveActor(const ActorReference& actor, const totk::core::WorldTransform& transform) {
    if (!actor.address()) return false;
    const auto& r = transform.rotation.values;
    const auto& p = transform.position;
    const float rows[12]{r[0], r[1], r[2], p.x, r[3], r[4], r[5], p.y, r[6], r[7], r[8], p.z};
    g_api.at<void(*)(std::uintptr_t, const float*, std::uint32_t)>(kSetMatrix)(actor.address(), rows, 0);
    return true;
}

}
