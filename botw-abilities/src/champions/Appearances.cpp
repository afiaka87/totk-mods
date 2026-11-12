// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "Appearances.hpp"
#include "AppearanceState.hpp"
#include "AppearanceObservation.hpp"
#include "ChampionAppearances.hpp"
#include "Effects.hpp"
#include "support/SafeRead.hpp"
#include "totk/port/CodeCheck.hpp"
#include <atomic>
#include <cmath>
#include <lib.hpp>

namespace botw_abilities::champions::appearances {
namespace {
using namespace totk::engine;
using namespace totk::port::literals;
namespace observe = appearance_observation;
using botw_abilities::support::safeRead;

// TotK 1.4.3 spawns copies whose models use the 1.4.x shader values (tools/add_appearances143.py).
constexpr std::array<const char*, 4> kActors143{
    "Npc_ChampionsPocMiphaLifecycle143", "Npc_ChampionsPocRevaliLifecycle143",
    "Npc_ChampionsPocDarukLifecycle143", "Npc_ChampionsPocUrbosaLifecycle143"};
constexpr bool namesCopy(const char* actor, const char* copy) {
    std::size_t i = 0;
    for (; actor[i]; ++i)
        if (actor[i] != copy[i]) return false;
    return copy[i] == '1' && copy[i + 1] == '4' && copy[i + 2] == '3' && !copy[i + 3];
}
static_assert([] {
    for (unsigned i = 0; i < kAppearances.size(); ++i)
        if (!namesCopy(kAppearances[i].actor, kActors143[i])) return false;
    return kAppearances.size() == kActors143.size();
}());
const char* actorName(unsigned index) { return totk::port::is143() ? kActors143[index] : kAppearances[index].actor; }

std::uintptr_t g_base{};
std::atomic<unsigned> g_epoch{};
struct Slot {
    AppearanceState state;
    std::atomic<bool> watching{}, unreadableReported{};
    std::atomic<float> frame{}, end{};
    bool placedReported{};
};
std::array<Slot, 4> g_slots;
std::array<ActorHandle, 4> g_actors;

void remove(ActorHandle actor) {
    reinterpret_cast<void(*)(std::uintptr_t)>(g_base + 0x83AA68_v121)(actor.address);
}

void clearPlayback(Slot& slot) {
    slot.watching.store(false, std::memory_order_release);
    slot.frame.store(0, std::memory_order_relaxed);
    slot.end.store(0, std::memory_order_relaxed);
    slot.unreadableReported.store(false, std::memory_order_relaxed);
}

void reportPlacement(unsigned index, const TransformService& transforms, ActorHandle actor,
                     const SceneContext& scene, const totk::core::WorldTransform& link) {
    auto& slot = g_slots[index];
    if (slot.placedReported) return;
    slot.placedReported = true;
    const auto placed = transforms.read(actor, scene.token);
    if (!placed) {
        Logging.Log("[champions] APPEARANCE_PLACED hero=%s error=%u", kAppearances[index].hero, unsigned(placed.error));
        return;
    }
    const auto& a = placed.value.position;
    const auto& l = link.position;
    const float dx = a.x - l.x, dy = a.y - l.y, dz = a.z - l.z;
    Logging.Log("[champions] APPEARANCE_PLACED hero=%s actor=(%f,%f,%f) link=(%f,%f,%f) distance=%f "
                "playing=%f frame=%f end=%f", kAppearances[index].hero, double(a.x), double(a.y), double(a.z),
                double(l.x), double(l.y), double(l.z), double(std::sqrt(dx * dx + dy * dy + dz * dz)),
                double(slot.state.waiting), double(slot.frame.load(std::memory_order_relaxed)),
                double(slot.end.load(std::memory_order_acquire)));
}

void spawn(unsigned index, const totk::core::WorldTransform& placement) {
    auto& slot = g_slots[index];
    slot.placedReported = false;
    clearPlayback(slot);
    slot.watching.store(true, std::memory_order_release);
    if (effects::spawnAppearance(actorName(index), placement)) return;
    clearPlayback(slot);
    slot.state = {};
    Logging.Log("[champions] APPEARANCE_REFUSED hero=%s", kAppearances[index].hero);
}

HOOK_DEFINE_TRAMPOLINE(AutoPlay) {
    static std::uintptr_t Callback(std::uintptr_t object, unsigned char* dirty, float frames) {
        const auto result = Orig(object, dirty, frames);
        const auto epoch = g_epoch.load(std::memory_order_acquire);
        for (unsigned i = 0; i < g_slots.size(); ++i) {
            auto& slot = g_slots[i];
            if (!slot.watching.load(std::memory_order_acquire)) continue;
            const auto sample = observe::inspectAnimation(safeRead, object, kAppearances[i].clip);
            if (!sample.matches || epoch != g_epoch.load(std::memory_order_acquire)) continue;
            if (!sample.readable) {
                if (!slot.unreadableReported.exchange(true))
                    Logging.Log("[champions] APPEARANCE_ANIMATION_UNREADABLE hero=%s", kAppearances[i].hero);
                continue;
            }
            slot.frame.store(sample.frame, std::memory_order_relaxed);
            slot.end.store(sample.end, std::memory_order_release);
            break;
        }
        return result;
    }
};
}

void reset() {
    g_epoch.fetch_add(1, std::memory_order_acq_rel);
    for (auto& slot : g_slots) {
        clearPlayback(slot);
        slot.state = {};
    }
    g_actors = {};
}

void used(Blessing blessing) { g_slots[unsigned(blessing)].state.request(); }
void beginFrame() { g_actors = {}; }

bool collect(const ActiveProcessView& actor) {
    for (unsigned i = 0; i < g_actors.size(); ++i) {
        if (!actor.processName.equals(actorName(i))) continue;
        if (!g_actors[i].address) g_actors[i] = actor.handle;
        else {
            Logging.Log("[champions] APPEARANCE_DUPLICATE hero=%s", kAppearances[i].hero);
            remove(actor.handle);
        }
        return true;
    }
    return false;
}

void service(const SceneContext& scene, const totk::core::WorldTransform& placement, float seconds) {
    const TransformService transforms(TransformFunctions::fromMainBase(g_base));
    for (unsigned i = 0; i < g_slots.size(); ++i) {
        auto& slot = g_slots[i];
        const auto actor = g_actors[i];
        const bool alive = actor.address && actor.status(scene.token) == HandleStatus::Current;
        const auto end = slot.end.load(std::memory_order_acquire);
        const bool finished = observe::playbackComplete(slot.frame.load(std::memory_order_relaxed), end);
        switch (slot.state.step(alive, finished, seconds)) {
        case AppearanceAction::Spawn:
            spawn(i, placement);
            break;
        case AppearanceAction::Arrived:
        case AppearanceAction::Follow: {
            const auto error = transforms.force(actor, scene.token, placement);
            if (error != TransformError::None)
                Logging.Log("[champions] APPEARANCE_FOLLOW_REFUSED hero=%s error=%u", kAppearances[i].hero, unsigned(error));
            // Read back a quarter second in, after earlier frames' forces have been applied.
            if (slot.state.waiting >= 0.25f) reportPlacement(i, transforms, actor, scene, placement);
            break;
        }
        case AppearanceAction::Delete:
            reportPlacement(i, transforms, actor, scene, placement);
            if (!finished) Logging.Log("[champions] APPEARANCE_CLEANUP hero=%s orphanOrTimeout=1", kAppearances[i].hero);
            slot.watching.store(false, std::memory_order_release);
            remove(actor);
            break;
        case AppearanceAction::Removed:
            clearPlayback(slot);
            break;
        case AppearanceAction::Timeout:
            clearPlayback(slot);
            Logging.Log("[champions] APPEARANCE_TIMEOUT hero=%s", kAppearances[i].hero);
            break;
        case AppearanceAction::None: break;
        }
    }
}

bool install(std::uintptr_t base) {
    constexpr totk::port::Site animation[]{{0x64B030, 0x39419008, 0x37000168}};
    if (!totk::port::codeMatches(base, animation, "champions", "APPEARANCE_ABI_REFUSED")) return false;
    g_base = base;
    AutoPlay::InstallAtOffset(0x64B030_v121);
    return true;
}
}
