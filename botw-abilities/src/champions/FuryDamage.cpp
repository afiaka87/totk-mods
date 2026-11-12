// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "FuryDamage.hpp"
#include "totk/port/CodeCheck.hpp"
#include "totk/engine/NativeRead.hpp"
#include <atomic>
#include <lib.hpp>

namespace botw_abilities::champions::fury_damage {
namespace {
using namespace totk::engine;
using namespace totk::engine::native_read;
using namespace totk::port::literals;
constexpr totk::port::Site kSites[]{
    {0x9E4838,0xA9BD7BFD,0xF9000BF5}, {0x9F4768,0xD10103FF,0xA9017BFD},
};
// The two hooked routines and the shock-reaction routine; 1.4.3 inlines all three, so they are 1.2.1 only.
constexpr totk::port::Site kSites121[]{
    {0x9E3EE0,0xD102C3FF,0x6D0423E9}, {0x9E333C,0xA9BE7BFD,0xA9014FF4},
    {0xA07954,0xA9BE7BFD,0xA9014FF4},
};
// 1.4.3 only: element chemical-update end, life damage-step end, and the shock start that 0xA07954 calls.
constexpr std::ptrdiff_t kReaction143 = 0x2FF944_v143, kDamage143 = 0x300BBC_v143,
                         kStartShock143 = 0x1C4E3CC_v143;
// InternalDamageCalculator::calculate's own packet types: 9 is condition (element) damage, 13 the
// custom internal damage that only equipment and special-part extensions compute, and 4 the normal
// physical damage. registerDamage (sub_9E4838) applies nothing unless both the type and the mask are
// non-zero, so a physical hit must carry type 4 with the game's own physical mask 0x40000; the mask
// is what the earlier bite build left at zero, so its damage registered but never reduced health
// (wolf-compact-11, Ryubing). Physical damage is not voided by armor the way elemental damage is.
constexpr std::uint32_t kPhysicalType = 4, kPhysicalMask = 0x40000;
std::uintptr_t g_base{}, g_scene{};
std::uint64_t g_generation{};
FuryHits<64> g_hits;
std::atomic_flag g_lock{};
std::atomic<bool> g_pending{};

struct Guard {
    Guard() { while (g_lock.test_and_set(std::memory_order_acquire)) {} }
    ~Guard() { g_lock.clear(std::memory_order_release); }
};
template<class Fn> Fn call(totk::port::Code offset) { return reinterpret_cast<Fn>(g_base + offset); }
template<class T> void write(void* base, std::size_t offset, const T& value) {
    std::memcpy(static_cast<std::byte*>(base) + offset, &value, sizeof(value));
}
FuryTarget identity(const void* actor) {
    return {reinterpret_cast<std::uintptr_t>(actor), read<std::uint32_t>(actor,0x10)};
}
std::optional<FuryBalance> take(const void* actor, bool reaction, bool ready) {
    if (!g_pending.load(std::memory_order_acquire)) return std::nullopt;
    Guard guard;
    return reaction ? g_hits.takeReaction(identity(actor),ready) : g_hits.takeDamage(identity(actor),ready);
}

void startReaction(void* info, const void* context) {
    if (!g_pending.load(std::memory_order_acquire)) return;
    const auto actor = read<const void*>(info,8);
    const auto condition = read<const void*>(info,16);
    const bool ready = !(read<std::uint8_t>(condition,40) & 4) &&
        read<float>(info,168) > 0 && call<bool(*)(const void*)>(0x9F4768)(info);
    // A temporary refusal leaves the request queued until its original deadline.
    const auto hit = take(actor,true,ready);
    if (!hit) return;
    const auto triggers = read<std::uint32_t>(info,40);
    const auto attack = read<std::uint64_t>(info,48);
    const auto attackType = read<std::uint32_t>(info,56);
    if (totk::port::is143()) {
        // The same writes and call the 1.2.1 routine makes for state 2.
        write(info,172,read<float>(info,168));
        write(info,40,triggers | 0x40u);
        write(info,44,read<std::uint32_t>(info,44) | 2u);
        reinterpret_cast<std::uintptr_t(*)(void*,const void*,unsigned,unsigned)>(
            g_base + static_cast<std::uintptr_t>(kStartShock143))(info,context,2,6);
        write(info,238,std::uint8_t{2});
    } else call<std::uintptr_t(*)(void*,const void*,unsigned)>(0xA07954)(info,context,2);
    // Keep the native shock state, but leave ordinary chemical damage to its original sources.
    write(info,40,triggers);
    write(info,48,attack);
    write(info,56,attackType);
    write(info,172,float(hit->reactionFrames));
    Logging.Log("[champions] FURY_REACTION actor=%p id=%u frames=%u",
        actor,identity(actor).id,unsigned(hit->reactionFrames));
}

void applyDamage(void* calculator, const void* context) {
    if (!g_pending.load(std::memory_order_acquire)) return;
    const auto actor = read<const void*>(calculator,8);
    const auto condition = read<const void*>(context,56);
    // registerDamage cannot add health damage while either native protection bit is set.
    const bool ready = !(read<std::uint8_t>(condition,40) & 4) &&
        !(read<std::uint32_t>(calculator,16) & 3);
    const auto hit = take(actor,false,ready);
    if (!hit) return;
    FuryDamagePacket packet{.damage = hit->damage};
    if (hit->physical) {
        // A bite is normal physical damage, paired with the game's physical mask so registerDamage
        // accumulates it into the target's health. Skipping the element extension keeps the humanoid
        // enemies' condition modifier (0x16F5210), which voids elemental damage behind armor, out of it.
        packet.type = kPhysicalType;
        packet.mask = kPhysicalMask;
    } else if (const auto extension = read<void*>(calculator,32)) {
        const auto vtable = read<const void*>(extension,0);
        using Modify = void(*)(void*,FuryDamagePacket*);
        read<Modify>(vtable,160)(extension,&packet);
    }
    call<void(*)(void*,const void*,const FuryDamagePacket*)>(0x9E4838)(calculator,context,&packet);
    Logging.Log("[champions] FURY_DAMAGE actor=%p id=%u requested=%u submitted=%d type=%u",
        actor,identity(actor).id,unsigned(hit->damage),packet.damage,packet.type);
}

HOOK_DEFINE_TRAMPOLINE(UpdateChemical) {
    static std::uintptr_t Callback(void* info, const void* context) {
        const auto result = Orig(info,context);
        startReaction(info,context);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(CalculateInternalDamage) {
    static void Callback(void* calculator, const void* context) {
        Orig(calculator,context);
        applyDamage(calculator,context);
    }
};

// Integer register `index` of the hooked code; `context->X[]` does not work on a float context.
std::uint64_t gameRegister(const exl::hook::InlineFloatCtx* context, unsigned index) {
    return *reinterpret_cast<const std::uint64_t*>(
        reinterpret_cast<const std::uint8_t*>(context) + 0x200 + index * 8);
}
// X21 = element array, W22 = updated element (256 bytes each); context at the bottom of the 0x4F0 frame.
HOOK_DEFINE_INLINE(UpdateChemical143) {
    static void Callback(exl::hook::InlineFloatCtx* context) {
        const auto index = std::int64_t(std::int32_t(gameRegister(context,22)));
        startReaction(reinterpret_cast<void*>(gameRegister(context,21) + std::uint64_t(index * 256)),
                      reinterpret_cast<const void*>(gameRegister(context,29) - 0x4F0));
    }
};
// X19 is the life object; its damage calculator and context are the parts 1.2.1 passes as arguments.
HOOK_DEFINE_INLINE(CalculateInternalDamage143) {
    static void Callback(exl::hook::InlineFloatCtx* context) {
        const auto life = gameRegister(context,19);
        applyDamage(reinterpret_cast<void*>(life + 0x1218),reinterpret_cast<const void*>(life + 0x18D0));
    }
};
bool sites143Match(std::uintptr_t base) {
    struct Word { std::ptrdiff_t offset; std::uint32_t expected; };
    constexpr Word kWords[]{
        {kReaction143,0x910006C8}, {kReaction143 + 4,0x92407D16},
        {kDamage143,0xF940DA75}, {kDamage143 + 4,0xB4000315},
        {kStartShock143,0xA9BE7BFD}, {kStartShock143 + 4,0xA9014FF4},
    };
    for (const auto& word : kWords) {
        const auto actual = totk::suite::checkedCode(base + static_cast<std::uintptr_t>(word.offset));
        if (actual == word.expected) continue;
        Logging.Log("[champions] Urbosa's Fury damage off on 1.4.3: offset=%p expected=%08x actual=%08x",
            reinterpret_cast<void*>(word.offset),word.expected,actual);
        return false;
    }
    return true;
}
}

bool queue(FuryTarget target, FuryBalance balance, std::uint64_t expires) {
    Guard guard;
    const bool accepted = g_hits.insert(target,balance,expires);
    if (accepted) g_pending.store(true,std::memory_order_release);
    return accepted;
}

void reset() {
    Guard guard;
    g_hits.clear();
    g_pending.store(false,std::memory_order_release);
    g_generation = g_scene = 0;
}

void service(std::uint64_t generation, std::uintptr_t scene, std::uint64_t now) {
    std::size_t expired{};
    {
        Guard guard;
        if (g_generation != generation || g_scene != scene) {
            g_hits.clear();
            g_generation = generation;
            g_scene = scene;
        }
        expired = g_hits.expire(now);
        g_pending.store(g_hits.pending(),std::memory_order_release);
    }
    if (expired) Logging.Log("[champions] FURY_HITS_EXPIRED count=%zu",expired);
}

bool install(std::uintptr_t base) {
    if (!totk::port::codeMatches(base,kSites,"champions","FURY_DAMAGE_BUILD_REFUSED")) return false;
    if (totk::port::is143()) {
        // Without the two hooks a queued hit expires unused: the ability runs but deals no damage.
        if (!sites143Match(base)) return true;
        g_base = base;
        UpdateChemical143::InstallAtOffset(kReaction143);
        CalculateInternalDamage143::InstallAtOffset(kDamage143);
        return true;
    }
    if (!totk::port::codeMatches(base,kSites121,"champions","FURY_DAMAGE_BUILD_REFUSED")) return false;
    g_base = base;
    UpdateChemical::InstallAtOffset(0x9E333C_v121);
    CalculateInternalDamage::InstallAtOffset(0x9E3EE0_v121);
    return true;
}
}
