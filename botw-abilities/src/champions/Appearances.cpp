// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "Appearances.hpp"
#include "AppearanceState.hpp"
#include "AppearanceObservation.hpp"
#include "ChampionAppearances.hpp"
#include "Effects.hpp"
#include "totk/engine/CodeCheck.hpp"
#include <atomic>
#include <lib.hpp>

namespace botw_abilities::champions::appearances {
namespace {
using namespace totk::engine;
namespace observe = appearance_observation;

std::uintptr_t g_base{};
std::atomic<unsigned> g_epoch{};
struct Slot {
    AppearanceState state;
    std::atomic<bool> watching{}, unreadableReported{};
    std::atomic<float> frame{}, end{};
};
std::array<Slot, 4> g_slots;
std::array<ActorHandle, 4> g_actors;

// Validate every mapped region before borrowing native animation memory on hardware.
bool safeRead(std::uintptr_t address, void* output, std::size_t size) {
    constexpr auto limit = std::uintptr_t{1} << 39;
    if (address < 0x1000 || address >= limit || size > limit - address) return false;
    for (auto cursor = address; cursor < address + size;) {
        MemoryInfo region{};
        std::uint32_t page{};
        if (svcQueryMemory(&region, &page, cursor) || !(region.perm & 1) || !region.size ||
            cursor < region.addr || cursor - region.addr >= region.size) return false;
        const auto end = region.addr + region.size;
        if (end <= cursor) return false;
        cursor = end;
    }
    std::memcpy(output, reinterpret_cast<const void*>(address), size);
    return true;
}

void remove(ActorHandle actor) {
    reinterpret_cast<void(*)(std::uintptr_t)>(g_base + 0x83AA68)(actor.address);
}

void clearPlayback(Slot& slot) {
    slot.watching.store(false, std::memory_order_release);
    slot.frame.store(0, std::memory_order_relaxed);
    slot.end.store(0, std::memory_order_relaxed);
    slot.unreadableReported.store(false, std::memory_order_relaxed);
}

void spawn(unsigned index, const totk::core::WorldTransform& placement) {
    auto& slot = g_slots[index];
    clearPlayback(slot);
    slot.watching.store(true, std::memory_order_release);
    if (effects::spawnAppearance(kAppearances[index].actor, placement)) return;
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
        if (!actor.processName.equals(kAppearances[i].actor)) continue;
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
            break;
        }
        case AppearanceAction::Delete:
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
    constexpr CodeSite animation[]{{0x64B030, 0x39419008, 0x37000168}};
    if (!codeMatches(base, animation, "champions", "APPEARANCE_ABI_REFUSED")) return false;
    g_base = base;
    AutoPlay::InstallAtOffset(0x64B030);
    return true;
}
}
