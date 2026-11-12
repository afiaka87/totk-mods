// SPDX-License-Identifier: MIT
// Copyright (c) HanSoloBlaster, afiaka87
#include "BombInstall.hpp"
#include <lib.hpp>
#include "totk/port/CodeCheck.hpp"
#include "totk/engine/Pointer.hpp"
#include "BombRuntime.hpp"
#include "BombRender.hpp"
#include "BombPhysics.hpp"
#include "BombBlast.hpp"
#include "BombHud.hpp"
#include "BombInventory.hpp"
#include "BombVisualBackend.hpp"

namespace bomb = remote_bomb::runtime;
using namespace totk::port::literals;
bool remote_bomb::chooseSphereFromWheel(std::uintptr_t screen) {
    return bomb::chooseFromWheel(screen,Shape::Sphere);
}
bool remote_bomb::chooseCubeFromWheel(std::uintptr_t screen) {
    return bomb::chooseFromWheel(screen,Shape::Cube);
}
namespace {
HOOK_DEFINE_TRAMPOLINE(BombNativeTimerHook) {
    static std::uintptr_t Callback(std::uintptr_t node, std::uintptr_t context) {
        return remote_bomb::engine::explosion::updateNativeTimer(
            [](std::uintptr_t n,std::uintptr_t c) { return Orig(n,c); },node,context);
    }
};
HOOK_DEFINE_TRAMPOLINE(BombPouchSortHook) {
    static std::uintptr_t Callback(std::uintptr_t pouch, int mode, unsigned category, std::uintptr_t list) {
        remote_bomb::inventory::prioritizePouch(exl::util::modules::GetTargetStart(), pouch, category, list);
        return Orig(pouch, mode, category, list);
    }
};
HOOK_DEFINE_TRAMPOLINE(BombMaterialSortHook) {
    static std::uintptr_t Callback(std::uintptr_t pouch, int mode, int order) {
        const auto result = Orig(pouch, mode, order);
        if (mode == 1)
            remote_bomb::inventory::prioritizeShortcut(exl::util::modules::GetTargetStart(), pouch);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(BombExplosionListClearHook) {
    static std::uintptr_t Callback(std::uintptr_t list) {
        return remote_bomb::engine::explosion::admitElementListClear(list) ? Orig(list) : 0;
    }
};
HOOK_DEFINE_TRAMPOLINE(BombExplosionDeathHook) {
    static void Callback(std::uintptr_t component) {
        if (remote_bomb::engine::explosion::admitDeathProcedure(component))
            Orig(component);
    }
};
HOOK_DEFINE_TRAMPOLINE(BombChemicalDamageHook) {
    static void Callback(std::uintptr_t calculator, std::uintptr_t parameters, std::uintptr_t hit, std::uintptr_t info) {
        remote_bomb::engine::explosion::reportChemicalDamage(info);
        Orig(calculator, parameters, hit, info);
    }
};
HOOK_DEFINE_TRAMPOLINE(BombBlastContactHook) {
    static void Callback(std::uintptr_t contact, std::uintptr_t calculation, void *output) {
        remote_bomb::engine::explosion::calculateContact(
            [](std::uintptr_t a, std::uintptr_t b, void *c) { Orig(a, b, c); }, contact, calculation, output);
    }
};
HOOK_DEFINE_TRAMPOLINE(BombElementInstanceHook) {
    static std::uintptr_t Callback(std::uintptr_t instance, void *argument) {
        const auto result = Orig(instance, argument);
        if (result)
            remote_bomb::engine::explosion::instanceCreated(instance);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(BombElementRemovalHook) {
    static std::uintptr_t Callback(std::uintptr_t instance) {
        remote_bomb::engine::explosion::instanceRemoved(instance);
        return Orig(instance);
    }
};
HOOK_DEFINE_TRAMPOLINE(BombElementPhysicsHook) {
    static void Callback(std::uintptr_t element, void *context, unsigned char initial) {
        Orig(element, context, initial);
        remote_bomb::engine::explosion::elementPhysicsPrepared(element, reinterpret_cast<std::uintptr_t>(context));
    }
};
HOOK_DEFINE_TRAMPOLINE(BombBodyAddedHook) {
    static bool Callback(std::uintptr_t sdkBody, std::uintptr_t body, unsigned char immediate) {
        const bool added = Orig(sdkBody, body, immediate);
        if (added)
            remote_bomb::physics::bodyAdded(body);
        return added;
    }
};
// 1.4.3 writes that add step into its caller, which returns nothing: a body absent before the call and present after it was added.
HOOK_DEFINE_TRAMPOLINE(BombBodyAdd143Hook) {
    static void Callback(std::uintptr_t body, unsigned immediate) {
        const auto before = remote_bomb::physics::inWorld(body);
        Orig(body, immediate);
        if (before && !*before && remote_bomb::physics::inWorld(body).value_or(false))
            remote_bomb::physics::bodyAdded(body);
    }
};
HOOK_DEFINE_TRAMPOLINE(BombMotionChangedHook) {
    static std::uintptr_t Callback(std::uintptr_t body, const int *preset,
                                   unsigned char registerChange, unsigned char keepActivation) {
        const auto result = Orig(body, preset, registerChange, keepActivation);
        remote_bomb::physics::motionChanged(body, preset);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(BombNpadHook) {
    static void Callback(void *device) { Orig(device); bomb::tick(device); }
};
HOOK_DEFINE_TRAMPOLINE(BombClockHook) {
    static void Callback(void *module, const void *pause, float frames) {
        Orig(module, pause, frames);
        bomb::publishPhysicsTime(frames);
    }
};
// On 1.4.3 the routine above misses one frame path; the frame-rate update runs every frame (scale at +16).
HOOK_DEFINE_TRAMPOLINE(BombFrameRateHook) {
    static void Callback(void *rate, float frames) {
        Orig(rate, frames);
        bomb::publishPhysicsTime(totk::engine::readMemory<float>(reinterpret_cast<std::uintptr_t>(rate) + 16));
    }
};
constexpr std::ptrdiff_t kFrameRateUpdate143 = 0x3E67F0_v143;
bool frameRateUpdateMatches(std::uintptr_t base) {
    const auto site = base + static_cast<std::uintptr_t>(kFrameRateUpdate143);
    const auto first = totk::suite::checkedCode(site), second = totk::suite::checkedCode(site + 4),
               store = totk::suite::checkedCode(site + 0x30);
    if (totk::engine::entryMatches(first, 0x6DBD23E9) &&
        (second == 0xA9017BFD || totk::engine::chainedAbsoluteTail(site, first)) && store == 0xBD001001)
        return true;
    Logging.Log("[remote-bomb] off on 1.4.3: the frame-rate update reads %08x,%08x store=%08x", first, second, store);
    return false;
}
HOOK_DEFINE_TRAMPOLINE(BombWheelOpenHook) {
    static float Callback(std::uintptr_t screen) {
        const auto result = Orig(screen);
        bomb::wheelOpened(screen);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(BombWheelExitHook) {
    static std::uintptr_t Callback(std::uintptr_t screen, unsigned char cancel) {
        const auto result = Orig(screen, cancel);
        bomb::wheelExited(screen, (cancel & 1) != 0);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(BombWheelCloseHook) {
    static std::uintptr_t Callback(std::uintptr_t screen) {
        const auto result = Orig(screen);
        bomb::wheelClosed(screen);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(BombNativeSelectionHook) {
    static std::uintptr_t Callback(std::uintptr_t screen) {
        const auto result = Orig(screen);
        bomb::nativeSelectionAccepted(screen);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(BombPlayerKeyHook) {
    static std::uintptr_t Callback(std::uintptr_t node) {
        const auto result = Orig(node);
        bomb::maskRecentAbilityKey(node);
        return result;
    }
};
HOOK_DEFINE_TRAMPOLINE(BombAbilityAdmissionHook) {
    static std::uint64_t Callback(std::uintptr_t actor) {
        const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
        return bomb::nativeAbilityAdmission([](std::uintptr_t player) { return Orig(player); }, actor, caller);
    }
};
HOOK_DEFINE_TRAMPOLINE(BombAmiiboHook) {
    static std::uint64_t Callback(std::uintptr_t node) {
        const auto result = Orig(node);
        bomb::cancelOwnedAmiibo(node);
        return result;
    }
};
}
bool remote_bomb::install(std::uintptr_t base) {
    if (totk::port::is143()) {
        // Without its clock the mod never sees the game running.
        if (!frameRateUpdateMatches(base))
            return false;
        // Places named outside the code checks below; one without a 1.4.3 entry would fault when used.
        if (!totk::port::all(0xB0F844, 0x74F09C, 0x7B9274, 0x4716E48, 0x46707E8, 0x462EF90, 0x4717048,
                             0x462E3D8, 0x4630020, 0x462E0F0, 0x462E1B8, 0x471DCB0, 0x4722920)) {
            Logging.Log("[remote-bomb] off on 1.4.3: a game place it names has no 1.4.3 entry");
            return false;
        }
    }
    if (!bomb::initialize(base) || !remote_bomb::physics::initialize(base) ||
        !remote_bomb::engine::explosion::initialize(base) || !remote_bomb::render::install(base) ||
        !remote_bomb::hud::install(base))
        return false;
    BombPouchSortHook::InstallAtOffset(0x1A7450C_v121);
    BombMaterialSortHook::InstallAtOffset(0x1A733D0_v121);
    BombNpadHook::InstallAtOffset(0x2A267BC_v121);
    if (totk::port::is143())
        BombFrameRateHook::InstallAtOffset(kFrameRateUpdate143);
    else
        BombClockHook::InstallAtOffset(0x7EDC00_v121);
    BombWheelOpenHook::InstallAtOffset(0x1B1DC94_v121);
    BombWheelExitHook::InstallAtOffset(0x19AD3F4_v121);
    BombWheelCloseHook::InstallAtOffset(0x1B1EEB8_v121);
    BombNativeSelectionHook::InstallAtOffset(0x1B1EEF8_v121);
    BombPlayerKeyHook::InstallAtOffset(0x1D30CBC_v121);
    BombAbilityAdmissionHook::InstallAtOffset(0x932610_v121);
    BombAmiiboHook::InstallAtOffset(0x1D29AAC_v121);
    // These nine hooks only act on bombs the mod simulates, so 1.4.3 skips them when both bombs are the game's own.
    if (!totk::port::is143() || !nativeBomb(Shape::Sphere) || !nativeBomb(Shape::Cube)) {
        if (totk::port::is143())
            BombBodyAdd143Hook::InstallAtOffset(remote_bomb::physics::kBodyAdd143);
        else
            BombBodyAddedHook::InstallAtOffset(0x23326FC_v121);
        BombMotionChangedHook::InstallAtOffset(0xAAD62C_v121);
        BombBlastContactHook::InstallAtOffset(0x1396A0C_v121);
        BombElementInstanceHook::InstallAtOffset(0x13884B0_v121);
        BombElementRemovalHook::InstallAtOffset(0x138B024_v121);
        BombElementPhysicsHook::InstallAtOffset(0x138B0D0_v121);
        BombChemicalDamageHook::InstallAtOffset(0x16FEB00_v121);
        BombExplosionListClearHook::InstallAtOffset(0x138FE84_v121);
        BombExplosionDeathHook::InstallAtOffset(0x15A935C_v121);
        if (totk::port::is143())
            Logging.Log("[remote-bomb] own-bomb physics and blast hooks installed on 1.4.3 (body add through 0x1882A0)");
    }
    BombNativeTimerHook::InstallAtOffset(0x1C1F768_v121);
    Logging.Log("[remote-bomb] automatic Materials, pinned sorting and remote detonation installed");
    return true;
}
#ifdef TOTK_SUITE_MEMBER
void remote_bomb::switched(bool on) { remote_bomb::runtime::setSwitchedOn(on); }
#endif
