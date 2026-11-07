// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "NativeLife.hpp"
#include "NativeActor.hpp"
#include "totk/engine/CodeCheck.hpp"
#include <algorithm>
#include <atomic>

namespace botw_abilities::wolf {
namespace {
using namespace totk::engine;
// game::component::Life::preCalc, where the game subtracts this frame's damage from the actor's life.
constexpr CodeSite kSites[]{{0x64B264, 0xD10143FF, 0xA9017BFD}};
// Life: the owning actor; the sums the game's own hits and the condition hits are about to subtract;
// the strongest hit's attacker link (shared link data holding the proc, then the id) and the link's id.
constexpr std::uintptr_t kActor = 24, kExternalDamage = 424, kInternalDamage = 4656;
constexpr std::uintptr_t kAttackerData = 448, kAttackerId = 456, kDataProc = 64, kDataId = 72;
constexpr unsigned kHitLogs = 40;
std::uintptr_t g_base{};
std::atomic<std::uintptr_t> g_wolf{}, g_player{};
std::atomic<int> g_damage{}, g_playerDamage{};
unsigned g_hitLogs{};

std::uintptr_t attacker(std::uintptr_t life) {
    const auto id = readMemory<std::int32_t>(life + kAttackerId);
    const auto data = readMemory<std::uintptr_t>(life + kAttackerData);
    if (id == -1 || !isPlausibleAddress(data) || readMemory<std::int32_t>(data + kDataId) != id) return 0;
    return readMemory<std::uintptr_t>(data + kDataProc);
}

void intercept(std::uintptr_t life) {
    const auto external = readMemory<std::int32_t>(life + kExternalDamage);
    const auto internal = readMemory<std::int32_t>(life + kInternalDamage);
    if (external <= 0 && internal <= 0) return;
    const auto source = attacker(life);
    const bool byPlayer = source && source == g_player.load(std::memory_order_acquire);
    const int total = std::max(external, 0) + std::max(internal, 0);
    if (byPlayer) g_playerDamage.fetch_add(total, std::memory_order_acq_rel);
    else g_damage.fetch_add(total, std::memory_order_acq_rel);
    writeMemory(life + kExternalDamage, std::int32_t{0});
    writeMemory(life + kInternalDamage, std::int32_t{0});
    if (g_hitLogs < kHitLogs) {
        ++g_hitLogs;
        Logging.Log("[wolf-link] LIFE_HIT external=%d internal=%d attacker=%p player=%u", external, internal,
            reinterpret_cast<void*>(source), unsigned(byPlayer));
    }
}

HOOK_DEFINE_TRAMPOLINE(LifePreCalc) {
    static void Callback(std::uintptr_t life) {
        const auto wolf = g_wolf.load(std::memory_order_acquire);
        if (wolf && readMemory<std::uintptr_t>(life + kActor) == wolf) intercept(life);
        Orig(life);
    }
};
}

bool installLifeApi(std::uintptr_t base) {
    if (g_base) return base == g_base;
    if (!codeMatches(base, kSites, "wolf-link", "LIFE_ABI_REFUSED")) return false;
    g_base = base;
    LifePreCalc::InstallAtOffset(0x64B264);
    return true;
}

void watchLife(std::uintptr_t wolf, std::uintptr_t player) {
    g_player.store(player, std::memory_order_release);
    g_wolf.store(wolf, std::memory_order_release);
}

LifeHits takeLifeHits() {
    return {g_damage.exchange(0, std::memory_order_acq_rel), g_playerDamage.exchange(0, std::memory_order_acq_rel)};
}

}
