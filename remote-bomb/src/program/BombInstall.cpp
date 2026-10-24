// SPDX-License-Identifier: MIT
// Copyright (c) Clay Mullis
#include "BombInstall.hpp"
#include <lib.hpp>
#include "BombRuntime.hpp"
#include "BombRender.hpp"
#include "BombPhysics.hpp"
#include "BombBlast.hpp"
#include "BombHud.hpp"
#include "BombInventory.hpp"

namespace bomb = remote_bomb::runtime;
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
    if (!bomb::initialize(base) || !remote_bomb::physics::initialize(base) ||
        !remote_bomb::engine::explosion::initialize(base) || !remote_bomb::render::install(base) ||
        !remote_bomb::hud::install(base))
        return false;
    BombPouchSortHook::InstallAtOffset(0x1A7450C);
    BombMaterialSortHook::InstallAtOffset(0x1A733D0);
    BombNpadHook::InstallAtOffset(0x2A267BC);
    BombClockHook::InstallAtOffset(0x7EDC00);
    BombWheelOpenHook::InstallAtOffset(0x1B1DC94);
    BombWheelExitHook::InstallAtOffset(0x19AD3F4);
    BombWheelCloseHook::InstallAtOffset(0x1B1EEB8);
    BombNativeSelectionHook::InstallAtOffset(0x1B1EEF8);
    BombPlayerKeyHook::InstallAtOffset(0x1D30CBC);
    BombAbilityAdmissionHook::InstallAtOffset(0x932610);
    BombAmiiboHook::InstallAtOffset(0x1D29AAC);
    BombBodyAddedHook::InstallAtOffset(0x23326FC);
    BombMotionChangedHook::InstallAtOffset(0xAAD62C);
    BombBlastContactHook::InstallAtOffset(0x1396A0C);
    BombElementInstanceHook::InstallAtOffset(0x13884B0);
    BombElementRemovalHook::InstallAtOffset(0x138B024);
    BombElementPhysicsHook::InstallAtOffset(0x138B0D0);
    BombChemicalDamageHook::InstallAtOffset(0x16FEB00);
    BombExplosionListClearHook::InstallAtOffset(0x138FE84);
    BombExplosionDeathHook::InstallAtOffset(0x15A935C);
    BombNativeTimerHook::InstallAtOffset(0x1C1F768);
    Logging.Log("[remote-bomb] automatic Materials, pinned sorting and remote detonation installed");
    return true;
}
