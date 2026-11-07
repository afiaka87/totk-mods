// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "NativeBrain.hpp"
#include "NativeGroundMotion.hpp"
#include "totk/engine/CodeCheck.hpp"
#include <atomic>

namespace botw_abilities::wolf {
namespace {
using namespace totk::engine;
constexpr CodeSite kSites[]{{0x6635B8, 0xD10483FF, 0x6D0B23E9}};
std::uintptr_t g_base{};
BrainUpdate g_update{};
std::atomic<std::uint32_t> g_selected{UINT32_MAX};
std::atomic_flag g_updating{};

struct UpdateGuard {
    ~UpdateGuard() { g_updating.clear(std::memory_order_release); }
};

HOOK_DEFINE_TRAMPOLINE(Scheduler) {
    static std::uintptr_t Callback(std::uintptr_t brain, float delta, float rate) {
        const auto address = readMemory<std::uintptr_t>(brain + 2928);
        if (!isWolfActor(address)) return Orig(brain, delta, rate);
        const auto generation = readMemory<std::uint32_t>(address + 0x10);
        if (generation != g_selected.load(std::memory_order_acquire)) return 0;
        if (g_updating.test_and_set(std::memory_order_acquire)) {
            Logging.Log("[wolf-link] AI_UPDATE_BUSY id=%u", generation);
            return 0;
        }
        UpdateGuard guard;
        if (generation != g_selected.load(std::memory_order_acquire)) return 0;
        const auto actor = ActorReference::fromLive(address);
        if (!actor.address() || actor.generation() != generation) {
            Logging.Log("[wolf-link] AI_REFERENCE_REFUSED actor=%p id=%u",
                reinterpret_cast<void*>(address), generation);
            return 0;
        }
        g_update(actor, {delta, rate});
        return 0;
    }
};
}

bool installBrainApi(std::uintptr_t base, BrainUpdate update) {
    if (g_base) return base == g_base && update == g_update;
    if (!update || !initializeActorApi(base) || !codeMatches(base, kSites, "wolf-link", "BRAIN_ABI_REFUSED")) {
        Logging.Log("[wolf-link] AI_INSTALL_REFUSED callback=%u", unsigned(update != nullptr));
        return false;
    }
    if (!installGroundMotionApi(base)) return false;
    g_update = update;
    g_base = base;
    Scheduler::InstallAtOffset(0x6635B8);
    return true;
}

void selectBrainOwner(std::uint32_t generation) { g_selected.store(generation, std::memory_order_release); }
bool isBrainOwner(std::uint32_t generation) {
    return generation != UINT32_MAX && generation == g_selected.load(std::memory_order_acquire);
}

}
