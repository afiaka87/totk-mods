// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "Fury.hpp"
#include "Combat.hpp"
#include "Effects.hpp"
#include "FuryRelease.hpp"
#include "FuryDamage.hpp"
#include "FuryTargets.hpp"
#include "HeavyFury.hpp"
#include "GameData.hpp"
#include "Runtime.hpp"
#include "UseEvents.hpp"
#include "totk/engine/ActorRoster.hpp"
#include "totk/engine/CodeCheck.hpp"
#include "totk/engine/NativeRead.hpp"
#include "totk/engine/Transform.hpp"
#include <lib.hpp>

namespace botw_abilities::champions::fury_native {
namespace {
using namespace totk::engine;
using namespace totk::engine::native_read;
constexpr CodeSite kSites[]{ {0x1725600,0xFC160FEE,0x6D0133ED}, {0x11BD100,0xF9411408,0xB4000068},
    {0x6836B0,0xF94C1808,0xB4000228},
    {0x1D545AC,0xA9BE7BFD,0xA9014FF4}, {0x1D54618,0xD10203FF,0x6D022BEB},
    {0x1D548C8,0xA9BE7BFD,0xA9014FF4}, {0xBC7610,0xF9400408,0xF9400508},
    {0xFA7F08,0xF9411408,0xB4000068}, {0xADDCF4,0x92407C28,0x7100203F},
    {0x1623BC4,0xD10083FF,0xA9017BFD}, {0x9F1B14,0xD101C3FF,0x6D0123E9},
    {0x1D2C004,0xF9402408,0xF9400108} };
constexpr CodeSite kTargetSites[]{{0x1327B90,0xF9411408,0xB4000168}};
std::uintptr_t g_base{};
UseEvents<4, FuryRequest> g_requests;
std::atomic<bool> g_active{};
HeavyFury g_heavy;

bool alive(std::uintptr_t actor) {
    const auto registry = readMemory<std::uintptr_t>(actor + layout::kActorComponentRegistry);
    const auto life = isPlausibleAddress(registry) ? readMemory<std::uintptr_t>(registry + 0x2F8) : 0;
    const auto holder = isPlausibleAddress(life) ? readMemory<std::uintptr_t>(life + 0x1830) : 0;
    return isPlausibleAddress(holder) && readMemory<int>(holder + 8) > 0;
}

bool requestHits(const SceneContext& scene, const FuryRequest& request) {
    const auto access = resolveActiveProcessRoster(g_base,scene.token);
    if (!access) {
        Logging.Log("[champions] FURY_TARGETS_REFUSED reason=roster error=%u",unsigned(access.error));
        return false;
    }
    unsigned requested = 0, full = 0, visuals = 0;
    const auto expires = simulationTick() + (std::uint64_t{60} << 32);
    const auto walk = visitActiveProcesses(access.value,[&](const ActiveProcessView& actor) {
        if (actor.processState != 4) return VisitControl::Continue;
        const auto balance = furyBalance(actor.processName.c_str());
        if (!balance) return VisitControl::Continue;
        const auto address = actor.handle.address;
        const auto position = readMemory<totk::core::WorldPosition>(address + layout::kActorPosition);
        if (!furyTargetInRange(request.placement.position,position,true) || !alive(address))
            return VisitControl::Continue;
        if (!reinterpret_cast<bool(*)(std::uintptr_t)>(g_base + 0x1327B90)(address))
            return VisitControl::Continue;
        if (!fury_damage::queue({address,readMemory<std::uint32_t>(address + 0x10)},*balance,expires)) {
            ++full;
            return VisitControl::Continue;
        }
        ++requested;
        auto placement = request.placement;
        placement.position = position;
        visuals += effects::strikeEnemy(placement);
        Logging.Log("[champions] FURY_TARGET name=%s damage=%u reaction=%u",
            actor.processName.c_str(),unsigned(balance->damage),unsigned(balance->reactionFrames));
        return VisitControl::Continue;
    });
    Logging.Log("[champions] FURY_TARGETS requested=%u overflow=%u visuals=%u complete=%u error=%u radius=%f",
        requested,full,visuals,unsigned(walk && walk.value.complete),
        walk ? 0 : unsigned(walk.error),double(kFuryTargetRadius));
    return requested != 0 || (walk && walk.value.complete);
}

void queueChargedImpact(const void* actor, unsigned char charged, unsigned level, const HeavyFuryKey* heavy = nullptr) {
    if (!(charged & 1)) return;
    const auto* name = read<const char*>(actor, layout::kActorNamePointer);
    if (!admit(name, 0, 7) || std::memcmp(name, "Player", 7) != 0) return;
    if (!heavy) {
        const auto equipment = reinterpret_cast<std::uintptr_t(*)(const void*)>(g_base + 0xFA7F08)(actor);
        if (!isPlausibleAddress(equipment)) {
            Logging.Log("[champions] FURY_REFUSED reason=equipment");
            return;
        }
        const auto type = reinterpret_cast<unsigned(*)(std::uintptr_t, unsigned)>(g_base + 0xADDCF4)(equipment, 0);
        // Heavy spins use stamina instead of native charge level; their finisher must not strike again.
        if (type == 2) return;
    }
    const auto saved = data::state().snapshot();
    const auto context = currentContext(Blessing::Urbosa);
    const auto scene = resolveScene(g_base);
    if (!scene) {
        Logging.Log("[champions] FURY_REFUSED reason=scene level=%u", level);
        return;
    }
    if (heavy && (heavy->generation != saved.generation || heavy->scene != scene.value.token.value)) {
        Logging.Log("[champions] FURY_HEAVY_REFUSED reason=stale-scene-or-save");
        return;
    }
    const auto players = resolvePlayerActors(scene.value);
    const bool owned = players && players.value.player.address == reinterpret_cast<std::uintptr_t>(actor);
    if (!saved.ready || !canReleaseFury(saved.states[3], context, level, owned)) {
        Logging.Log("[champions] FURY_REFUSED ready=%u owned=%u level=%u admission=%u",
            unsigned(saved.ready), unsigned(owned), level, unsigned(admission(saved.states[3], context)));
        return;
    }
    const auto transform = TransformService(TransformFunctions::fromMainBase(g_base)).read(
        players.value.player, scene.value.token);
    if (!transform) {
        Logging.Log("[champions] FURY_REFUSED reason=transform error=%u", unsigned(transform.error));
        return;
    }
    const bool accepted = g_requests.push({saved.generation, scene.value.token.value, transform.value});
    Logging.Log("[champions] FURY_RELEASE level=%u heavy=%u queued=%u", level, unsigned(heavy != nullptr), unsigned(accepted));
}

void sampleHeavySpin(std::uintptr_t action, bool entering) {
    const auto actor = reinterpret_cast<std::uintptr_t(*)(std::uintptr_t)>(g_base + 0xBC7610)(action);
    const auto saved = data::state().snapshot();
    const auto context = currentContext(Blessing::Urbosa);
    const auto scene = resolveScene(g_base);
    if (!scene) {
        g_heavy.end(action);
        if (entering) Logging.Log("[champions] FURY_HEAVY_REFUSED reason=scene error=%u", unsigned(scene.error));
        return;
    }
    const auto players = resolvePlayerActors(scene.value);
    const bool owned = players && players.value.player.address == actor;
    if (!saved.ready || !scene || !owned || admission(saved.states[3], context) != Refusal::None) {
        g_heavy.end(action);
        if (entering)
            Logging.Log("[champions] FURY_HEAVY_REFUSED ready=%u scene=%u owned=%u admission=%u",
                unsigned(saved.ready), unsigned(bool(scene)), unsigned(owned), unsigned(admission(saved.states[3], context)));
        return;
    }
    const auto* component = reinterpret_cast<const void*(*)(std::uintptr_t)>(g_base + 0x11BD100)(actor);
    if (!admit(component, 0x13E4, 8)) {
        g_heavy.end(action);
        if (entering) Logging.Log("[champions] FURY_HEAVY_REFUSED reason=stamina-component");
        return;
    }
    // StaminaCalculator at Player+0x1388 publishes normal/extra stamina at +0x5c/+0x60.
    const float stamina = read<float>(component, 0x13E4) + read<float>(component, 0x13E8);
    const HeavyFuryKey key{action, actor, scene.value.token.value, saved.generation};
    if (entering) {
        const bool accepted = g_heavy.begin(key, stamina);
        Logging.Log("[champions] FURY_HEAVY_BEGIN accepted=%u stamina=%f threshold=%f",
            unsigned(accepted), double(stamina), double(HeavyFury::kRequiredStamina));
    } else if (g_heavy.advance(key, stamina, true)) {
        Logging.Log("[champions] FURY_HEAVY_THRESHOLD stamina=%f", double(stamina));
    }
    // The native charge scheduler uses virtual key 9 for the held attack input.
    const bool attackHeld = reinterpret_cast<bool(*)(const void*, unsigned)>(g_base + 0x1D2C004)(component, 9);
    if (g_heavy.release(key, attackHeld)) {
        Logging.Log("[champions] FURY_HEAVY_RELEASE stamina=%f", double(stamina));
        queueChargedImpact(reinterpret_cast<const void*>(actor), 1, 1, &key);
    }
}

bool serviceRequests(const SceneContext& scene, Context context) {
    const auto generation = data::state().snapshot().generation;
    bool struck = false;
    FuryRequest request;
    while (g_requests.pop(generation,scene.token.value,request)) {
        const auto released = releaseFury(data::state(),request.generation,context,
            [&] { return requestHits(scene,request); });
        if (released) {
            struck = true;
            effects::launchStrike(request.placement);
            notifyUsed({released.claim.generation,scene.token.value,Blessing::Urbosa,released.claim.use.state.charges == 0});
        }
        Logging.Log("[champions] FURY_STRIKE requested=%u committed=%u refusal=%u charges=%u",
            unsigned(released.spawned),unsigned(released.committed),unsigned(released.claim.use.refusal),
            unsigned(data::state().snapshot().states[3].charges));
    }
    return struck;
}

void updateCharge(const ActorHandle& player, Context context, bool struck) {
    const auto saved = data::state().snapshot();
    const auto* component = reinterpret_cast<const void*(*)(std::uintptr_t)>(g_base+0x11BD100)(player.address);
    const auto level = read<unsigned>(component,0x1144);
    const bool heavy = g_heavy.charging();
    const bool charging = !struck && saved.ready && (heavy || (read<unsigned>(component,2780) & 0x40000000u)) &&
        canReleaseFury(saved.states[3],context,heavy ? 1 : level,true);
    const bool previous = g_active.exchange(charging,std::memory_order_acq_rel);
    if (charging != previous)
        Logging.Log("[champions] FURY_CHANNEL active=%u level=%u charges=%u",unsigned(charging),level,unsigned(saved.states[3].charges));
    effects::hold(Effect::UrbosaAura,charging);
    effects::hold(Effect::UrbosaRange,charging);
}

HOOK_DEFINE_TRAMPOLINE(ChargedImpact) {
    static std::uintptr_t Callback(const void* actor, unsigned char charged, unsigned level, unsigned char extra) {
        const auto result = Orig(actor, charged, level, extra);
        queueChargedImpact(actor, charged, level);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(HeavySpinEnter) {
    static std::uintptr_t Callback(std::uintptr_t action) {
        const auto result = Orig(action);
        sampleHeavySpin(action, true);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(HeavySpinUpdate) {
    static float Callback(std::uintptr_t action) {
        const float result = Orig(action);
        sampleHeavySpin(action, false);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(HeavySpinLeave) {
    static float Callback(std::uintptr_t action) {
        // Release can transition out of the spin before another update callback.
        sampleHeavySpin(action, false);
        const float result = Orig(action);
        g_heavy.end(action);
        return result;
    }
};
} // namespace

bool active() { return g_active.load(std::memory_order_acquire); }
void reset() {
    g_requests.clear();
    g_heavy.reset();
    fury_damage::reset();
    g_active.store(false,std::memory_order_release);
    effects::hold(Effect::UrbosaAura,false);
    effects::hold(Effect::UrbosaRange,false);
}

void service(const SceneContext& scene, const ActorHandle& player, Context context) {
    fury_damage::service(data::state().snapshot().generation,scene.token.value,simulationTick());
    const bool struck = serviceRequests(scene, context);
    updateCharge(player, context, struck);
}

bool install(std::uintptr_t base) {
    if (!codeMatches(base,kSites,"champions","FURY_BUILD_REFUSED")) return false;
    if (!codeMatches(base,kTargetSites,"champions","FURY_TARGET_BUILD_REFUSED")) return false;
    if (!fury_damage::install(base)) return false;
    g_base = base;
    ChargedImpact::InstallAtOffset(0x1725600);
    HeavySpinEnter::InstallAtOffset(0x1D545AC);
    HeavySpinUpdate::InstallAtOffset(0x1D54618);
    HeavySpinLeave::InstallAtOffset(0x1D548C8);
    return true;
}
} // namespace botw_abilities::champions::fury_native
