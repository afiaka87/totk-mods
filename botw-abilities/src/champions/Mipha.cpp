// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "Mipha.hpp"
#include "FatalHealing.hpp"
#include "GameData.hpp"
#include "Runtime.hpp"
#include "totk/engine/CodeCheck.hpp"
#include "totk/engine/Scene.hpp"
#include <lib.hpp>

namespace botw_abilities::champions::mipha {
namespace {
using namespace totk::engine;
constexpr CodeSite kSites[]{
    {0x1701670, 0xD100C3FF, 0xA9017BFD}, {0x170173C, 0xF9400408, 0xB40003A8},
    {0x273854C, 0xB940042A, 0x52A8C069}, {0x64C31C, 0xF94C1809, 0xB4000229},
};
std::uintptr_t g_base{};
template <class Fn> Fn call(std::uintptr_t offset) { return reinterpret_cast<Fn>(g_base + offset); }

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
    if (!codeMatches(base, kSites, "champions", "MIPHA_BUILD_REFUSED")) return false;
    g_base = base;
    Fatal::InstallAtOffset(0x1701670);
    return true;
}
} // namespace botw_abilities::champions::mipha
