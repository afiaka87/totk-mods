// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "Daruk.hpp"
#include "Blocking.hpp"
#include "Combat.hpp"
#include "Effects.hpp"
#include "GameData.hpp"
#include "Runtime.hpp"
#include "totk/engine/ActorReference121.hpp"
#include "totk/engine/ActorRoster.hpp"
#include "totk/engine/CodeCheck.hpp"
#include "totk/engine/NativeRead.hpp"
#include <algorithm>
#include <optional>
#include <lib.hpp>

namespace botw_abilities::champions::daruk {
namespace {
using namespace totk::engine;
using namespace totk::engine::native_read;
constexpr CodeSite kSites[]{
    {0x9E8454,0xA9BC7BFD,0xF9000BF7}, {0xA07690,0xD10183FF,0xA9027BFD},
    {0x16F2A44,0xA9BA7BFD,0xA9016FFC}, {0xC9C920,0xA9BE7BFD,0xF9000BF3},
    {0x753530,0xA9BB7BFD,0xF9000BF9}, {0x86A288,0xA9BD7BFD,0xA90157F6},
    {0x679BAC,0xD10143FF,0xA9017BFD}, {0x64EEEC,0xB9405808,0x71001D1F},
    {0x276E6B4,0xB9405828,0x51001909}, {0x9EEB38,0xD10303FF,0xA9087BFD},
};
std::uintptr_t g_base{};
BlockLedger<> g_blocks;
GuardContacts<> g_guardContacts;
std::atomic<bool> g_active{};
std::array<std::atomic<std::uint64_t>, 3> g_reported{};
bool g_previous{};
bool g_shieldHeld{};
unsigned g_previousCharges{};
template<class Fn> Fn call(std::uintptr_t offset) { return reinterpret_cast<Fn>(g_base + offset); }

struct Gate {
    Context context;
    std::uint64_t generation = 0, scene = 0, flags = 0;
    const void* player = nullptr;
    const void* equipment = nullptr;
    explicit operator bool() const { return player != nullptr; }
};

Gate live() {
    const auto saved = data::state().snapshot();
    const auto context = currentContext(Blessing::Daruk);
    if (!saved.ready || !context.gameplay || context.shrine || !saved.states[2].enabled) return {};
    const auto scene = resolveScene(g_base);
    if (!scene) return {};
    const auto players = resolvePlayerActors(scene.value);
    if (!players) return {};
    const auto* player = reinterpret_cast<const void*>(players.value.player.address);
    const auto* equipment = read<const void*>(read<const void*>(player, 0x228), 0x230);
    const auto flags = read<std::uint64_t>(equipment, 0x4600);
    return guardingFromEquipment(flags) ? Gate{context, saved.generation, scene.value.token.value, flags, player, equipment} : Gate{};
}

bool namedPlayer(const void* actor) {
    const auto* name = read<const char*>(actor, layout::kActorNamePointer);
    return admit(name, 0, 7) && std::memcmp(name, "Player", 7) == 0;
}

BlockResult block(const Gate& gate, AttackKey key, unsigned source) {
    const auto result = g_blocks.block(data::state(), gate.generation, gate.scene,
        gate.context, guardingFromEquipment(gate.flags), key, simulationTick());
    const auto signature = (std::uint64_t(key.primary.serial) << 32) | key.secondary.serial;
    const auto report = signature ^ (std::uint64_t(result.status) << 56);
    if (result.status == BlockStatus::Blocked || g_reported[source].exchange(report, std::memory_order_relaxed) != report)
        Logging.Log("[champions] DARUK_HIT source=%u status=%u primary=%u/%u secondary=%u/%u charges=%u",
            source, unsigned(result.status), key.primary.serial, key.primary.kind, key.secondary.serial,
            key.secondary.kind, result.status == BlockStatus::Blocked ? unsigned(result.claim.use.state.charges) : unsigned(data::state().snapshot().states[2].charges));
    if (result.status == BlockStatus::Blocked)
        notifyUsed({result.claim.generation, gate.scene, Blessing::Daruk, result.claim.use.state.charges == 0});
    return result;
}

IncomingHit incoming(const void* hit) {
    return {read<std::uint32_t>(hit,20), read<std::uint32_t>(hit,96),
            read<std::uint32_t>(hit,120), read<std::uint8_t>(hit,224)};
}

std::uintptr_t clearReaction(void* result) {
    const unsigned none = 0;
    std::memcpy(static_cast<std::byte*>(result)+88,&none,sizeof(none));
    return call<std::uintptr_t(*)(void*)>(0x64EEEC)(result);
}

bool protectedGuard(const Gate& gate, const void* result) {
    return g_guardContacts.protectedPrimary(gate.generation,gate.scene,simulationTick(),read<AttackId>(result,92));
}

struct Contacts { const void* data = nullptr; unsigned count = 0; bool valid = true; };
Contacts contacts(const void* actor) {
    const auto* sensor = read<const void*>(read<const void*>(actor,0x228),0x428);
    const auto* buffer = read<const void*>(sensor,88);
    if (!buffer) return {};
    const auto count = read<int>(buffer,24), capacity = read<int>(buffer,8);
    const auto used = std::min(count,capacity);
    const auto* data = read<const void*>(buffer,16);
    const bool valid = count >= 0 && capacity >= 0 && used <= 128 &&
        (!used || admit(data,0,std::size_t(used)*168));
    return {data, valid ? unsigned(used) : 0, valid};
}

bool shieldContactsProtected(const Gate& gate, const void* shield) {
    auto rows = contacts(shield);
    const auto* equipment = read<const void*>(read<const void*>(shield,0x228),0x208);
    std::optional<ActorReference121> attachment;
    if (equipment) {
        attachment.emplace(g_base, at(equipment,176));
        if (const auto* actor = attachment->get()) {
            const auto alternative = contacts(actor);
            if (!alternative.valid || alternative.count) rows = alternative;
        }
    }
    if (!rows.valid || !rows.count) {
        Logging.Log("[champions] DARUK_WEAR_REFUSED reason=contacts valid=%u count=%u", unsigned(rows.valid), rows.count);
        return false;
    }
    bool all = true, any = false;
    unsigned skipped = 0;
    for (unsigned i = 0; i < rows.count; ++i) {
        const auto* row = at(rows.data, std::size_t(i)*168);
        ActorReference121 source(g_base, at(row,96));
        const auto* actor = source.get();
        if (!actor) { ++skipped; continue; }
        const auto* components = read<const void*>(actor,0x228);
        if (!read<const void*>(components,0x428) || !read<const void*>(components,760)) { ++skipped; continue; }
        AttackKey key;
        const bool found = call<bool(*)(const void*, AttackId*, AttackId*)>(0x679BAC)(actor, &key.primary, &key.secondary);
        any = true;
        if (!found || !key) {
            all = false;
            Logging.Log("[champions] DARUK_WEAR_REFUSED reason=source-id row=%u", i);
        } else all = bool(block(gate,key,2)) && all;
    }
    if (!any) Logging.Log("[champions] DARUK_WEAR_REFUSED reason=no-attacker rows=%u skipped=%u", rows.count, skipped);
    return any && all;
}

bool protectDamage(const void* calculator, const void* hit) {
    const auto* actor = read<const void*>(calculator,8);
    if (namedPlayer(actor) && activeHit(incoming(hit))) {
        const auto gate = live();
        const AttackKey key{read<AttackId>(hit,4),read<AttackId>(hit,12)};
        if (gate && gate.player == actor) {
            if (block(gate,key,0)) return true;
        }
    }
    return false;
}

bool protectGuardReaction(const void* calculator, void* result, const void* info) {
    const auto* actor = read<const void*>(calculator,120);
    const bool harmful = read<int>(info,28) > 0 || read<unsigned>(info,24) != 0 || read<std::uint64_t>(info,16) != 0;
    if (namedPlayer(actor)) {
        const auto gate = live();
        if (gate && gate.player == actor) {
            const AttackKey key{read<AttackId>(info,0),read<AttackId>(info,8)};
            const bool protectedHit = harmful && bool(block(gate,key,1));
            if (!g_guardContacts.record(gate.generation,gate.scene,simulationTick(),key.primary,protectedHit))
                Logging.Log("[champions] DARUK_GUARD_FOLLOWUP_REFUSED reason=missing-id-or-capacity primary=%u",key.primary.serial);
            if (protectedHit) {
                std::memcpy(static_cast<std::byte*>(result)+92,&key.primary,sizeof(key.primary));
                return true;
            }
        }
    }
    return false;
}

template <class ApplyNativeWear>
void applyEquipmentWear(void* extension, void* hit, ApplyNativeWear applyNativeWear) {
    const auto* actor = read<const void*>(extension,8);
    const auto* shield = read<const void*>(read<const void*>(actor,0x228),0x438);
    if (!shield || !admit(extension,0,32)) { applyNativeWear(extension); return; }
    const auto gate = live();
    const auto slot = guardSlot(gate.flags);
    if (!gate || slot >= 8) {
        applyNativeWear(extension);
        return;
    }
    ActorReference121 guarded(g_base, at(gate.equipment,0x20+0x18*slot));
    if (guarded.get() != actor) { applyNativeWear(extension); return; }
    const auto probe = probeEquipmentWear(std::span<const std::byte,32>{static_cast<const std::byte*>(extension),32},
        applyNativeWear, [&] { return activeHit(incoming(hit)); },
        [&] { return shieldContactsProtected(gate,actor); },
        [&] { call<void(*)(void*)>(0xC9C920)(hit); });
    if (probe.flagDelta) __atomic_fetch_or(static_cast<unsigned char*>(extension)+24,probe.flagDelta,__ATOMIC_RELAXED);
}

HOOK_DEFINE_TRAMPOLINE(RegisterDamage) {
    static std::uintptr_t Callback(const void* calculator, const void* context, const void* hit) {
        if (protectDamage(calculator, hit)) return reinterpret_cast<std::uintptr_t>(calculator);
        return Orig(calculator, context, hit);
    }
};

HOOK_DEFINE_TRAMPOLINE(GuardReaction) {
    static std::uintptr_t Callback(const void* calculator, void* result, const void* context,
                                   const void* source, const void* info) {
        if (protectGuardReaction(calculator, result, info)) return clearReaction(result);
        return Orig(calculator, result, context, source, info);
    }
};

HOOK_DEFINE_TRAMPOLINE(FinalGuardReaction) {
    static std::uintptr_t Callback(void* extension, void* result) {
        const auto* actor = read<const void*>(extension,8);
        if (namedPlayer(actor)) {
            const auto gate = live();
            if (gate && gate.player == actor && protectedGuard(gate,result)) {
                clearReaction(result);
                return 1;
            }
        }
        return Orig(extension,result);
    }
};

HOOK_DEFINE_TRAMPOLINE(GuardChemicalContact) {
    static std::uintptr_t Callback(const void* chemical, const void* result) {
        const auto* actor = read<const void*>(chemical,24);
        if (namedPlayer(actor) && read<unsigned>(result,88) == 0) {
            const auto gate = live();
            if (gate && gate.player == actor && protectedGuard(gate,result)) {
                Logging.Log("[champions] DARUK_CHEMICAL_BLOCKED primary=%u", read<AttackId>(result,92).serial);
                return reinterpret_cast<std::uintptr_t>(chemical);
            }
        }
        return Orig(chemical,result);
    }
};

HOOK_DEFINE_TRAMPOLINE(EquipmentWear) {
    static void Callback(void* extension, void* hit) {
        const auto applyNativeWear = [hit](void* prefix) { Orig(prefix, hit); };
        applyEquipmentWear(extension, hit, applyNativeWear);
    }
};
} // namespace

bool active() { return g_active.load(std::memory_order_acquire); }
void reset() {
    g_previous = false; g_previousCharges = 0; g_shieldHeld = false;
    g_active.store(false,std::memory_order_release);
    for (auto& reported : g_reported) reported.store(0,std::memory_order_relaxed);
    effects::hold(Effect::DarukShell,false);
    effects::hold(Effect::DarukAura,false);
}

void service(const ActorHandle& player, Context context, bool shieldHeld, bool freshInput) {
    if (freshInput) g_shieldHeld = shieldHeld;
    const auto saved = data::state().snapshot();
    const auto* components = read<const void*>(reinterpret_cast<const void*>(player.address),0x228);
    const auto* equipment = read<const void*>(components,0x230);
    const auto scene = resolveScene(g_base);
    const bool guarding = guardingFromEquipment(read<std::uint64_t>(equipment,0x4600));
    const bool armed = scene && g_blocks.observeGuard(saved.generation,scene.value.token.value,g_shieldHeld);
    const bool enabled = saved.ready && barrier(saved.states[2],context,guarding && armed);
    const unsigned charges = saved.states[2].charges;
    if (enabled && !g_previous) {
        effects::pulse(Effect::DarukArrive,1.2f);
        effects::pulse(Effect::DarukFlare,1.5f);
    }
    // Each blocked hit spends a charge; the shell plays Sidon's break burst, including on the last one.
    if (g_previous && charges < g_previousCharges) effects::pulse(Effect::DarukShellBreak,1.0f);
    if (!enabled && g_previous) effects::pulse(Effect::DarukLeave,1.2f);
    effects::hold(Effect::DarukAura,enabled);
    effects::hold(Effect::DarukShell,enabled);
    if (enabled != g_previous)
        Logging.Log("[champions] DARUK_BARRIER active=%u charges=%u", unsigned(enabled),unsigned(saved.states[2].charges));
    g_previous = enabled; g_previousCharges = charges;
    g_active.store(enabled,std::memory_order_release);
}

bool install(std::uintptr_t base) {
    if (!codeMatches(base,kSites,"champions","DARUK_BUILD_REFUSED")) return false;
    g_base = base;
    RegisterDamage::InstallAtOffset(0x9E8454);
    GuardReaction::InstallAtOffset(0xA07690);
    FinalGuardReaction::InstallAtOffset(0x276E6B4);
    GuardChemicalContact::InstallAtOffset(0x9EEB38);
    EquipmentWear::InstallAtOffset(0x16F2A44);
    return true;
}
} // namespace botw_abilities::champions::daruk
