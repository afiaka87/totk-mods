// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "Mipha.hpp"
#include "FatalHealing.hpp"
#include "GameData.hpp"
#include "Runtime.hpp"
#include "totk/port/CodeCheck.hpp"
#include "totk/engine/Scene.hpp"
#include <atomic>
#include <lib.hpp>

namespace botw_abilities::champions::mipha {
namespace {
using namespace totk::engine;
using namespace totk::port::literals;
constexpr totk::port::Site kSites[]{
    {0x1701670, 0xD100C3FF, 0xA9017BFD}, {0x170173C, 0xF9400408, 0xB40003A8},
    {0x273854C, 0xB940042A, 0x52A8C069},
};
// The routine that applies a received life message; 1.4.3 has none, so it is checked and called on 1.2.1 only.
constexpr totk::port::Site kSites121[]{{0x64C31C, 0xF94C1809, 0xB4000229}};
// TotK 1.4.3 only: the routine the game calls on the life condition after it raises the bonus hearts.
constexpr std::ptrdiff_t kMarkLifeChanged143 = 0x1B3E748_v143;
std::uintptr_t g_base{};
template <class Fn> Fn call(totk::port::Code offset) { return reinterpret_cast<Fn>(g_base + offset); }

// 1.4.3 inlines 1.2.1's 0x64C31C; this does its Mipha branch. The message stays pending; a repeated raise does nothing.
std::uintptr_t applyMessage143(std::uintptr_t life, std::uintptr_t, std::uint8_t) {
    const auto holder = readMemory<std::uintptr_t>(life + 6208);
    if (!isPlausibleAddress(holder) || !readMemory<std::uint8_t>(life + 44)) return 0;
    const auto flags = readMemory<std::uint16_t>(life + 68);
    const auto amount = readMemory<std::int32_t>(life + 60);
    if ((flags & 0xC03) != 0x800 || readMemory<std::int32_t>(life + 52) >= 1 || amount < 1) return 0;
    std::atomic_ref<std::int32_t> hearts(*reinterpret_cast<std::int32_t*>(holder + 8));
    auto current = hearts.load();
    while (current < amount && !hearts.compare_exchange_weak(current, amount)) {}
    const auto actor = readMemory<std::uintptr_t>(life + 24);
    const auto registry = isPlausibleAddress(actor) ? readMemory<std::uintptr_t>(actor + 552) : 0;
    const auto condition = isPlausibleAddress(registry) ? readMemory<std::uintptr_t>(registry + 424) : 0;
    if (isPlausibleAddress(condition))
        reinterpret_cast<void(*)(std::uintptr_t)>(g_base + static_cast<std::uintptr_t>(kMarkLifeChanged143))(condition);
    return 0;
}

HOOK_DEFINE_TRAMPOLINE(Fatal) {
    static std::int32_t Callback(std::uintptr_t extension, std::uintptr_t context) {
        const auto actor = isPlausibleAddress(extension) ? readMemory<std::uintptr_t>(extension + 8) : 0;
        const auto name = isPlausibleAddress(actor) ? readMemory<const char*>(actor + layout::kActorNamePointer) : nullptr;
        if (!isPlausibleStringAddress(reinterpret_cast<std::uintptr_t>(name)) || std::strcmp(name, "Player") != 0 ||
            !isPlausibleAddress(context)) return Orig(extension, context);
        const auto scene = resolveScene(g_base);
        const auto generation = data::state().snapshot().generation;
        const auto result = healFatal(data::state(), currentContext(Blessing::Mipha),
            [&] { return Orig(extension, context); },
            [&] { return call<bool(*)(std::uintptr_t, std::uintptr_t)>(0x170173C)(extension, context); },
            [&] {
                return prepareMiphaHealing(extension, context,
                    {call<bool(*)(std::uintptr_t, const LifeMessage*)>(0x273854C),
                     totk::port::is143() ? &applyMessage143 :
                     call<std::uintptr_t(*)(std::uintptr_t, std::uintptr_t, std::uint8_t)>(0x64C31C)});
            },
            [&](std::uint64_t generation) {
                if (scene) notifyUsed({generation, scene.value.token.value, Blessing::Mipha, true});
            }, generation);
        // An uncharged death repeats its refusal every frame; log each change once and count the repeats.
        static std::uint32_t last = UINT32_MAX;
        static unsigned repeats{};
        const auto key = unsigned(result.source) | unsigned(result.refusal) << 8 |
                         unsigned(result.healing) << 16 | unsigned(result.charged) << 24;
        if (!result.charged && key == last) {
            ++repeats;
            return result.health;
        }
        Logging.Log("[champions] MIPHA_FATAL source=%u refusal=%u healing=%u charged=%u health=%d prior_repeats=%u",
                    unsigned(result.source), unsigned(result.refusal), unsigned(result.healing),
                    unsigned(result.charged), result.health, repeats);
        last = key;
        repeats = 0;
        return result.health;
    }
};
}
bool install(std::uintptr_t base) {
    if (!totk::port::codeMatches(base, kSites, "champions", "MIPHA_BUILD_REFUSED")) return false;
    if (totk::port::is143()) {
        const auto site = base + static_cast<std::uintptr_t>(kMarkLifeChanged143);
        const auto first = totk::suite::checkedCode(site), second = totk::suite::checkedCode(site + 4);
        if (first != 0xF940AC08 || second != 0xB4000088) {
            Logging.Log("[champions] Mipha's Grace off on 1.4.3: the life-changed routine reads %08x,%08x", first, second);
            return true;
        }
    } else if (!totk::port::codeMatches(base, kSites121, "champions", "MIPHA_BUILD_REFUSED")) return false;
    g_base = base;
    Fatal::InstallAtOffset(0x1701670_v121);
    return true;
}
} // namespace botw_abilities::champions::mipha
